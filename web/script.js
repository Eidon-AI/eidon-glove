// Global variables for serial communication
let port;
let reader;
let keepReading = true;
let decoder = new TextDecoder();
let inputBuffer = '';
const MAX_JOINTS = 16;

// Joint values array
let jointValues = new Array(MAX_JOINTS).fill(0);

// Recording functionality
let isRecording = false;
let recordingStartTime = 0;
let recordedMovement = {
    version: 1,
    movement: []
};
let recordingInterval = null;
const RECORDING_SAMPLE_RATE = 50; // ms between samples (20 samples per second)

// Playback control
let isPlaying = false;
let ignoreExternalInput = false; // Flag to ignore glove/gamepad input during playback

// DOM elements
const connectButton = document.getElementById('connect-button');
const disconnectButton = document.getElementById('disconnect-button');
const baudRateSelect = document.getElementById('baudRate');
const statusIndicator = document.getElementById('status-indicator');
const jointsContainer = document.getElementById('joints-container');
const logContainer = document.getElementById('log-container');
const canvasContainer = document.getElementById('canvas-container');

// View control buttons
const frontViewBtn = document.getElementById('front-view-btn');
const sideViewBtn = document.getElementById('side-view-btn');
const topViewBtn = document.getElementById('top-view-btn');
const resetViewBtn = document.getElementById('reset-view-btn');

// Three.js variables
let scene, camera, renderer, controls;
let hand = {
    palm: null,
    fingers: []
};

// Joint mapping information with inversion flags
const fingerJointMap = [
    // Thumb (4 joints)
    { finger: 0, joint: 0, type: 'CMC_ABDUCTION', min: 0, max: 255, inverted: false },
    { finger: 0, joint: 1, type: 'CMC_FLEXION', min: 0, max: 255, inverted: false },
    { finger: 0, joint: 2, type: 'MCP_FLEXION', min: 0, max: 255, inverted: false },
    { finger: 0, joint: 3, type: 'IP_FLEXION', min: 0, max: 255, inverted: false },
    
    // Index finger (3 joints)
    { finger: 1, joint: 0, type: 'MCP_ABDUCTION', min: 0, max: 160, inverted: false },
    { finger: 1, joint: 1, type: 'MCP_FLEXION', min: 0, max: 255, inverted: false },
    { finger: 1, joint: 2, type: 'PIP_FLEXION', min: 0, max: 255, inverted: false },
    
    // Middle finger (3 joints)
    { finger: 2, joint: 0, type: 'MCP_ABDUCTION', min: 0, max: 160, inverted: false },
    { finger: 2, joint: 1, type: 'MCP_FLEXION', min: 0, max: 255, inverted: false },
    { finger: 2, joint: 2, type: 'PIP_FLEXION', min: 0, max: 255, inverted: false },
    
    // Ring finger (3 joints)
    { finger: 3, joint: 0, type: 'MCP_ABDUCTION', min: 0, max: 160, inverted: false },
    { finger: 3, joint: 1, type: 'MCP_FLEXION', min: 0, max: 255, inverted: false },
    { finger: 3, joint: 2, type: 'PIP_FLEXION', min: 0, max: 255, inverted: false },
    
    // Pinky finger (3 joints)
    { finger: 4, joint: 0, type: 'MCP_ABDUCTION', min: 0, max: 160, inverted: false },
    { finger: 4, joint: 1, type: 'MCP_FLEXION', min: 0, max: 255, inverted: false },
    { finger: 4, joint: 2, type: 'PIP_FLEXION', min: 0, max: 255, inverted: false }
];

// Add HID variables
let hidDevice = null;
const REPORT_ID = 1;
const REPORT_SIZE = 24; // 2 bytes report ID + 2 bytes buttons + 20 bytes axes

// Add at the start of the file, with other global variables
let lastConnectedDeviceId = localStorage.getItem('lastHidDevice');

// Add these variables at the top of the file with other globals
let lastLinearX = 128;
let lastLinearY = 128;
let lastLinearZ = 128;

// Initialize Three.js scene
function initThreeJS() {
    // Check if canvasContainer exists
    if (!canvasContainer) {
        console.error('Canvas container not found');
        return;
    }

    // Create scene
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0xf0f0f0);
    
    // Create camera
    camera = new THREE.PerspectiveCamera(
        75,
        canvasContainer.clientWidth / canvasContainer.clientHeight,
        0.1,
        1000
    );
    camera.position.set(0, 15, 15);
    camera.lookAt(0, 0, 0);
    
    // Create renderer
    renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(canvasContainer.clientWidth, canvasContainer.clientHeight);
    renderer.setPixelRatio(window.devicePixelRatio);
    
    // Clear any existing canvas
    while (canvasContainer.firstChild) {
        canvasContainer.removeChild(canvasContainer.firstChild);
    }
    
    canvasContainer.appendChild(renderer.domElement);
    
    // Add orbit controls
    controls = new THREE.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.25;
    controls.enabled = true;  // Explicitly enable controls
    
    // Add lights
    const ambientLight = new THREE.AmbientLight(0x404040);
    scene.add(ambientLight);
    
    const directionalLight = new THREE.DirectionalLight(0xffffff, 0.5);
    directionalLight.position.set(1, 1, 1);
    scene.add(directionalLight);
    
    const directionalLight2 = new THREE.DirectionalLight(0xffffff, 0.3);
    directionalLight2.position.set(-1, 1, -1);
    scene.add(directionalLight2);
    
    // Add a grid helper
    const gridHelper = new THREE.GridHelper(20, 20);
    scene.add(gridHelper);
    
    // Create hand model
    createHandModel();
    
    // Handle window resize
    window.addEventListener('resize', onWindowResize);
    
    // Start animation loop
    animate();
}

// Modify createHandModel to store direct references to rotation groups
function createHandModel() {
    // Create materials
    const palmMaterial = new THREE.MeshPhongMaterial({ color: 0xf5c396 });
    const fingerMaterial = new THREE.MeshPhongMaterial({ color: 0xf5c396 });
    const jointMaterial = new THREE.MeshPhongMaterial({ color: 0xe3a977 });
    
    // Create palm
    const palmGeometry = new THREE.BoxGeometry(7, 1, 8);
    hand.palm = new THREE.Mesh(palmGeometry, palmMaterial);
    hand.palm.position.set(0, 0, 0);
    hand.palm.rotation.x = Math.PI; // Rotate 180 degrees around X axis
    scene.add(hand.palm);
    
    // Finger dimensions
    const fingerWidth = 1;
    const fingerHeight = 0.8;
    const fingerSegmentLengths = [3, 2, 1.5];
    const thumbSegmentLengths = [2, 2, 1.5];
    
    const fingerBasePositions = [
        [4, 1.5, -2],    // Thumb
        [1.5, -0.5, -4], // Index
        [0, -0.5, -4],   // Middle
        [-1.5, -0.5, -4],// Ring
        [-3, -0.5, -4]   // Pinky
    ];
    
    // Create fingers with direct rotation groups
    hand.fingers = [];
    
    for (let f = 0; f < 5; f++) {
        const finger = {
            name: ['Thumb', 'Index', 'Middle', 'Ring', 'Pinky'][f],
            base: new THREE.Group(), // Base group for finger position
            rotationGroups: [], // Store rotation groups directly
            segments: []
        };
        
        // Set finger base position
        finger.base.position.set(...fingerBasePositions[f]);
        hand.palm.add(finger.base);
        
        // Create segments with rotation groups
        const segmentLengths = f === 0 ? thumbSegmentLengths : fingerSegmentLengths;
        let parentGroup = finger.base;
        
        for (let s = 0; s < segmentLengths.length; s++) {
            // Create rotation group for this segment
            const rotationGroup = new THREE.Group();
            parentGroup.add(rotationGroup);
            finger.rotationGroups.push(rotationGroup);
            
            // Create segment
            const segmentGroup = new THREE.Group();
            rotationGroup.add(segmentGroup);
            
            // Create joint sphere
            const jointGeometry = new THREE.SphereGeometry(fingerWidth * 0.6, 8, 8);
            const joint = new THREE.Mesh(jointGeometry, jointMaterial);
            segmentGroup.add(joint);
            
            // Create segment box
            const segmentGeometry = new THREE.BoxGeometry(fingerWidth, fingerHeight, segmentLengths[s]);
            const segment = new THREE.Mesh(segmentGeometry, fingerMaterial);
            segment.position.z = -segmentLengths[s] / 2;
            segmentGroup.add(segment);
            
            finger.segments.push(segmentGroup);
            
            // Create next parent group at end of current segment
            if (s < segmentLengths.length - 1) {
                const nextParent = new THREE.Group();
                nextParent.position.z = -segmentLengths[s];
                segmentGroup.add(nextParent);
                parentGroup = nextParent;
            }
        }
        
        hand.fingers.push(finger);
    }
    
    // Add labels
    addFingerLabels();
    addHandLabel();
}

// Function to add finger labels
function addFingerLabels() {
        const fingerNames = ['Thumb', 'Index', 'Middle', 'Ring', 'Pinky'];
        
        for (let i = 0; i < hand.fingers.length; i++) {
            const finger = hand.fingers[i];
            
        // Skip if this finger doesn't have a group
        if (!finger.base) continue;
            
            // Create a canvas element
            const canvas = document.createElement('canvas');
            const context = canvas.getContext('2d');
            canvas.width = 128;
            canvas.height = 32;
            
            // Draw text on the canvas
            context.fillStyle = '#ffffff';
            context.fillRect(0, 0, canvas.width, canvas.height);
            context.font = 'Bold 16px Arial';
            context.fillStyle = '#000000';
            context.textAlign = 'center';
            context.textBaseline = 'middle';
        context.fillText(finger.name, canvas.width / 2, canvas.height / 2);
            
            // Create texture from canvas
            const texture = new THREE.CanvasTexture(canvas);
            
            // Create a plane to display the texture
            const geometry = new THREE.PlaneGeometry(2, 0.5);
            const material = new THREE.MeshBasicMaterial({ 
                map: texture,
                transparent: true,
                side: THREE.DoubleSide
            });
            const label = new THREE.Mesh(geometry, material);
            
            // Position the label above the finger
        label.position.set(0, 1.5, -2);
            label.rotation.x = Math.PI / 2; // Make it face up
            
        finger.base.add(label);
    }
}

// Function to add a hand label
function addHandLabel() {
    // Create a canvas element
    const canvas = document.createElement('canvas');
    const context = canvas.getContext('2d');
    canvas.width = 256;
    canvas.height = 64;
    
    // Draw text on the canvas
    context.fillStyle = '#ffffff';
    context.fillRect(0, 0, canvas.width, canvas.height);
    context.font = 'Bold 24px Arial';
    context.fillStyle = '#000000';
    context.textAlign = 'center';
    context.textBaseline = 'middle';
    context.fillText('RIGHT HAND (PALM UP)', canvas.width / 2, canvas.height / 2);
    
    // Create texture from canvas
    const texture = new THREE.CanvasTexture(canvas);
    
    // Create a plane to display the texture
    const geometry = new THREE.PlaneGeometry(7, 1.75);
    const material = new THREE.MeshBasicMaterial({ 
        map: texture,
        transparent: true,
        side: THREE.DoubleSide
    });
    const label = new THREE.Mesh(geometry, material);
    
    // Position the label below the hand
    label.position.set(0, -2, 0);
    label.rotation.x = Math.PI / 2; // Make it face up
    
    scene.add(label);
}

// Simplified updateHandModel function with direct rotation access
function updateHandModel() {
    if (!scene || !camera || !renderer) return;
    
    // Process each joint
    for (let i = 0; i < MAX_JOINTS; i++) {
        const jointInfo = fingerJointMap[i];
        if (!jointInfo) continue;
        
        const { finger, type, min, max } = jointInfo;
        const value = jointValues[i];
        const currentFinger = hand.fingers[finger];
        
        if (!currentFinger || !currentFinger.rotationGroups) continue;
        
        // Calculate normalized angle
        let angle;
        if (type.includes('FLEXION')) {
            const normalizedValue = (value - min) / (max - min);
            angle = normalizedValue * Math.PI / 2; // 90 degrees max
        } else if (type.includes('ABDUCTION')) {
            const normalizedAbduction = (value - 127) / 127; // -1 to 1 range
            angle = normalizedAbduction * Math.PI / 4; // ±45 degrees
        }
            
        // Apply rotation based on joint type
            if (finger === 0) { // Thumb
            switch (type) {
                case 'CMC_ABDUCTION':
                    currentFinger.base.rotation.z = Math.PI / 2.5 - angle;
                    currentFinger.base.rotation.y = -Math.PI / 6 - (angle * 0.5);
                    break;
                case 'CMC_FLEXION':
                    currentFinger.rotationGroups[0].rotation.x = angle;
                    break;
                case 'MCP_FLEXION':
                    currentFinger.rotationGroups[1].rotation.x = angle;
                    break;
                case 'IP_FLEXION':
                    currentFinger.rotationGroups[2].rotation.x = angle;
                    break;
            }
        } else { // Other fingers
            switch (type) {
                case 'MCP_ABDUCTION':
                    // Calculate base angle as before
                    const normalizedAbduction = (value - 127) / 127; // -1 to 1 range
                    let baseAngle = normalizedAbduction * Math.PI / 4; // ±45 degrees
                    
                    // Adjust angle range based on finger
                    switch (finger) {
                        case 1: // Index
                            baseAngle *= 0.5; // ±22.5 degrees
                            break;
                        case 2: // Middle
                            baseAngle *= 0.3; // ±13.5 degrees
                            break;
                        case 3: // Ring
                            baseAngle *= 0.3; // ±13.5 degrees (inverted)
                            break;
                        case 4: // Pinky
                            baseAngle *= 0.5; // ±22.5 degrees (inverted)
                            break;
                    }
                    currentFinger.rotationGroups[0].rotation.y = baseAngle;
                    break;
                case 'MCP_FLEXION':
                    currentFinger.rotationGroups[0].rotation.x = angle;
                    break;
                case 'PIP_FLEXION':
                    currentFinger.rotationGroups[1].rotation.x = angle;
                    break;
            }
        }
    }
    
    // Force update of the entire scene graph
    hand.palm.updateMatrixWorld(true);
    renderer.render(scene, camera);
}

// Handle window resize
function onWindowResize() {
    if (!camera || !renderer || !canvasContainer) return;  // Add guard clause
    
    camera.aspect = canvasContainer.clientWidth / canvasContainer.clientHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(canvasContainer.clientWidth, canvasContainer.clientHeight);
}

// Animation loop - remove the continuous model updates
function animate() {
    requestAnimationFrame(animate);
    
    if (controls) {
    controls.update();
    }
    
    // Always render the scene to keep it responsive
    if (renderer && scene && camera) {
    renderer.render(scene, camera);
    }
}

// Camera view controls
frontViewBtn.addEventListener('click', () => {
    camera.position.set(0, 0, 20);
    camera.lookAt(0, 0, 0);
    controls.update();
});

sideViewBtn.addEventListener('click', () => {
    camera.position.set(20, 0, 0);
    camera.lookAt(0, 0, 0);
    controls.update();
});

topViewBtn.addEventListener('click', () => {
    camera.position.set(0, 20, 0);
    camera.lookAt(0, 0, 0);
    controls.update();
});

resetViewBtn.addEventListener('click', () => {
    camera.position.set(10, 10, 10);
    camera.lookAt(0, 0, 0);
    controls.update();
});

// Event listeners for serial connection
connectButton.addEventListener('click', connectToDevice);
disconnectButton.addEventListener('click', disconnectFromDevice);

// Check if Web HID API is supported
if (!navigator.hid) {
    statusIndicator.textContent = 'Status: WebHID API not supported in this browser';
    connectButton.disabled = true;
    addLogMessage('ERROR: WebHID API is not supported in this browser. Try Chrome or Edge.');
}

// Initialize Three.js scene
initThreeJS();

// Initialize joint elements
initializeJointElements();

// Add this to the <style> section in the HTML file
const styleElement = document.createElement('style');
styleElement.textContent = `
.invert-toggle {
    display: block;
    margin-top: 5px;
    font-size: 0.8em;
    color: #666;
}

.invert-toggle input {
    margin-right: 5px;
}
`;
document.head.appendChild(styleElement);

// Add a button to check gamepad details
function addGamepadDiagnosticButton() {
    // Check if button already exists
    if (document.getElementById('gamepad-info-btn')) return;
    
    const diagnosticButton = document.createElement('button');
    diagnosticButton.textContent = 'Gamepad Info';
    diagnosticButton.id = 'gamepad-info-btn';
    diagnosticButton.className = 'control-button';
    diagnosticButton.onclick = showGamepadInfo;
    
    // Find or create the view-controls container
    let viewControls = document.querySelector('.view-controls');
    if (!viewControls) {
        viewControls = document.createElement('div');
        viewControls.className = 'view-controls';
        const controlsContainer = document.querySelector('.controls') || document.body;
        controlsContainer.appendChild(viewControls);
    }
    
    // Add button to view-controls
    viewControls.appendChild(diagnosticButton);
}

// Function to show gamepad information
function showGamepadInfo() {
    const gamepads = navigator.getGamepads();
    let infoText = 'Gamepad Information:\n\n';
    
    if (!gamepads || gamepads.length === 0 || !gamepads.some(gp => gp !== null)) {
        infoText += 'No gamepads detected. Please connect a gamepad first.';
    } else {
        for (let i = 0; i < gamepads.length; i++) {
            const gp = gamepads[i];
            if (gp) {
                infoText += `Gamepad ${i}:\n`;
                infoText += `- ID: ${gp.id}\n`;
                infoText += `- Connected: ${gp.connected}\n`;
                infoText += `- Axes: ${gp.axes.length}\n`;
                infoText += `- Buttons: ${gp.buttons.length}\n`;
                infoText += `- Mapping: ${gp.mapping}\n\n`;
                
                infoText += 'Axes Values:\n';
                gp.axes.forEach((value, index) => {
                    infoText += `- Axis ${index}: ${value.toFixed(4)}\n`;
                });
                
                infoText += '\n';
            }
        }
    }
    
    // Display the information
    if (typeof addLogMessage === 'function') {
        addLogMessage(infoText);
    } else {
        console.log(infoText);
        // Create a simple modal to show the info if addLogMessage doesn't exist
        const modal = document.createElement('div');
        modal.style.position = 'fixed';
        modal.style.top = '50%';
        modal.style.left = '50%';
        modal.style.transform = 'translate(-50%, -50%)';
        modal.style.backgroundColor = 'white';
        modal.style.padding = '20px';
        modal.style.border = '1px solid black';
        modal.style.zIndex = '1000';
        modal.style.maxHeight = '80vh';
        modal.style.overflow = 'auto';
        modal.style.whiteSpace = 'pre-wrap';
        modal.style.fontFamily = 'monospace';
        
        const closeButton = document.createElement('button');
        closeButton.textContent = 'Close';
        closeButton.style.display = 'block';
        closeButton.style.marginTop = '10px';
        closeButton.onclick = () => document.body.removeChild(modal);
        
        modal.textContent = infoText;
        modal.appendChild(closeButton);
        document.body.appendChild(modal);
    }
}

// Call this at the end of your initialization
function initGamepadSupport() {
    // Check if the Gamepad API is supported
    if (!navigator.getGamepads) {
        console.log('WARNING: Gamepad API is not supported in this browser.');
        if (typeof addLogMessage === 'function') {
            addLogMessage('WARNING: Gamepad API is not supported in this browser.');
        }
    } else {
        console.log('Gamepad API is supported. Connect a gamepad to begin.');
        if (typeof addLogMessage === 'function') {
            addLogMessage('Gamepad API is supported. Connect a gamepad to begin.');
        }
        
        // Add the diagnostic button
        // addGamepadDiagnosticButton();
    }
}

// Make sure the DOM is fully loaded before initializing
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initGamepadSupport);
} else {
    initGamepadSupport();
}

// Recording functions
function startRecording() {
    if (isRecording) return; // Already recording
    
    isRecording = true;
    recordingStartTime = Date.now();
    recordedMovement = {
        version: 1,
        movement: []
    };
    
    // Add initial frame
    recordFrame();
    
    // Set up interval for recording frames
    recordingInterval = setInterval(recordFrame, RECORDING_SAMPLE_RATE);
    
    // Show recording indicator
    showRecordingIndicator(true);
    
    addLogMessage("Recording started");
    updateRecordingButtonStates();
}

function stopRecording() {
    if (!isRecording) return; // Not recording
    
    isRecording = false;
    clearInterval(recordingInterval);
    recordingInterval = null;
    
    // Hide recording indicator
    showRecordingIndicator(false);
    
    addLogMessage(`Recording stopped. Captured ${recordedMovement.movement.length} frames.`);
    updateRecordingButtonStates();
}

function showRecordingIndicator(show) {
    let indicator = document.getElementById('recording-indicator');
    
    if (!indicator && show) {
        // Create indicator if it doesn't exist
        indicator = document.createElement('div');
        indicator.id = 'recording-indicator';
        indicator.style.position = 'fixed';
        indicator.style.top = '10px';
        indicator.style.right = '10px';
        indicator.style.width = '15px';
        indicator.style.height = '15px';
        indicator.style.borderRadius = '50%';
        indicator.style.backgroundColor = '#ff0000';
        indicator.style.boxShadow = '0 0 5px #ff0000';
        indicator.style.animation = 'pulse 1s infinite';
        indicator.style.zIndex = '1000';
        
        // Add pulse animation
        const style = document.createElement('style');
        style.textContent = `
            @keyframes pulse {
                0% { opacity: 1; }
                50% { opacity: 0.5; }
                100% { opacity: 1; }
            }
        `;
        document.head.appendChild(style);
        
        document.body.appendChild(indicator);
    } else if (indicator && !show) {
        // Remove indicator
        indicator.remove();
    }
}

function recordFrame() {
    // Create a frame with current timestamp and joint values
    const frame = {
        timestamp: Date.now() - recordingStartTime, // Relative time in ms
        joints: [...jointValues] // Clone the current joint values
    };
    
    // Add to recording
    recordedMovement.movement.push(frame);
}

function saveRecording() {
    if (recordedMovement.movement.length === 0) {
        addLogMessage("No recording to save");
        return;
    }
    
    // Convert to JSON string
    const jsonString = JSON.stringify(recordedMovement, null, 2);
    
    // Create a blob and download link
    const blob = new Blob([jsonString], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    
    // Create download link
    const a = document.createElement('a');
    a.href = url;
    a.download = `hand_movement_${new Date().toISOString().replace(/[:.]/g, '-')}.json`;
    document.body.appendChild(a);
    a.click();
    
    // Clean up
    setTimeout(() => {
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }, 100);
    
    addLogMessage("Recording saved");
}

// Playback variables
let playbackStartTime = 0;
let playbackInterval = null;
let currentPlaybackIndex = 0;

function loadRecording() {
    // Create file input element
    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.accept = '.json';
    fileInput.style.display = 'none';
    
    fileInput.addEventListener('change', (event) => {
        const file = event.target.files[0];
        if (!file) return;
        
        const reader = new FileReader();
        reader.onload = (e) => {
            try {
                const data = JSON.parse(e.target.result);
                
                // Validate the recording format
                if (data.version !== 1 || !Array.isArray(data.movement)) {
                    throw new Error('Invalid recording format');
                }
                
                // Store the loaded recording
                recordedMovement = data;
                addLogMessage(`Recording loaded: ${file.name} (${data.movement.length} frames)`);
                updateRecordingButtonStates();
                
            } catch (error) {
                addLogMessage(`Error loading recording: ${error.message}`);
            }
        };
        
        reader.readAsText(file);
    });
    
    // Trigger file selection
    document.body.appendChild(fileInput);
    fileInput.click();
    
    // Clean up
    setTimeout(() => {
        document.body.removeChild(fileInput);
    }, 100);
}

function startPlayback() {
    if (isPlaying || recordedMovement.movement.length === 0) return;
    
    isPlaying = true;
    ignoreExternalInput = true; // Ignore glove/gamepad input during playback
    playbackStartTime = Date.now();
    currentPlaybackIndex = 0;
    
    // Start playback interval
    playbackInterval = setInterval(updatePlayback, 16); // ~60fps
    
    // Show playback indicator
    showPlaybackIndicator(true);
    
    addLogMessage("Playback started");
    updateRecordingButtonStates();
}

function stopPlayback() {
    if (!isPlaying) return;
    
    isPlaying = false;
    ignoreExternalInput = false; // Resume processing glove/gamepad input
    clearInterval(playbackInterval);
    playbackInterval = null;
    
    // Hide playback indicator
    showPlaybackIndicator(false);
    
    addLogMessage("Playback stopped");
    updateRecordingButtonStates();
}

function showPlaybackIndicator(show) {
    let indicator = document.getElementById('playback-indicator');
    
    if (!indicator && show) {
        // Create indicator if it doesn't exist
        indicator = document.createElement('div');
        indicator.id = 'playback-indicator';
        indicator.style.position = 'fixed';
        indicator.style.top = '10px';
        indicator.style.right = '30px';
        indicator.style.width = '15px';
        indicator.style.height = '15px';
        indicator.style.borderRadius = '50%';
        indicator.style.backgroundColor = '#28a745';
        indicator.style.boxShadow = '0 0 5px #28a745';
        indicator.style.animation = 'pulse 1s infinite';
        indicator.style.zIndex = '1000';
        
        // Add text label
        const label = document.createElement('div');
        label.textContent = 'PLAYBACK MODE - External Input Disabled';
        label.style.position = 'fixed';
        label.style.top = '10px';
        label.style.right = '55px';
        label.style.color = '#28a745';
        label.style.fontWeight = 'bold';
        label.style.fontSize = '12px';
        label.style.zIndex = '1000';
        label.id = 'playback-label';
        
        document.body.appendChild(indicator);
        document.body.appendChild(label);
    } else if (indicator && !show) {
        // Remove indicator
        indicator.remove();
        
        // Remove label
        const label = document.getElementById('playback-label');
        if (label) label.remove();
    }
}

function updatePlayback() {
    if (recordedMovement.movement.length === 0) return;
    
    const elapsedTime = Date.now() - playbackStartTime;
    const movement = recordedMovement.movement;
    
    // Find the appropriate frame based on elapsed time
    while (currentPlaybackIndex < movement.length - 1 && 
           movement[currentPlaybackIndex + 1].timestamp <= elapsedTime) {
        currentPlaybackIndex++;
    }
    
    // If we've reached the end of the recording
    if (currentPlaybackIndex >= movement.length - 1 && 
        elapsedTime > movement[movement.length - 1].timestamp + 500) { // Add a small delay at the end
        stopPlayback();
        addLogMessage("Playback completed");
        return;
    }
    
    // Get current frame
    const currentFrame = movement[currentPlaybackIndex];
    
    // If there's a next frame, interpolate between frames
    if (currentPlaybackIndex < movement.length - 1) {
        const nextFrame = movement[currentPlaybackIndex + 1];
        const frameDuration = nextFrame.timestamp - currentFrame.timestamp;
        
        if (frameDuration > 0) {
            const frameProgress = (elapsedTime - currentFrame.timestamp) / frameDuration;
            
            // Interpolate joint values
            for (let i = 0; i < Math.min(currentFrame.joints.length, jointValues.length); i++) {
                const startValue = currentFrame.joints[i];
                const endValue = nextFrame.joints[i];
                jointValues[i] = Math.round(startValue + (endValue - startValue) * frameProgress);
                
                // Update the joint display
                updateJointDisplay(i, jointValues[i]);
            }
        } else {
            // If frames have the same timestamp, just use current frame
            applyFrame(currentFrame);
        }
    } else {
        // If this is the last frame, just apply it directly
        applyFrame(currentFrame);
    }
    
    // Update the hand model
    updateHandModel();
}

function applyFrame(frame) {
    // Apply joint values from the frame
    for (let i = 0; i < Math.min(frame.joints.length, jointValues.length); i++) {
        jointValues[i] = frame.joints[i];
        
        // Update the joint display
        updateJointDisplay(i, jointValues[i]);
    }
}

function updateRecordingButtonStates() {
    // Get all buttons
    const startRecordBtn = document.getElementById('start-record-btn');
    const stopRecordBtn = document.getElementById('stop-record-btn');
    const saveRecordBtn = document.getElementById('save-record-btn');
    const loadRecordBtn = document.getElementById('load-record-btn');
    const startPlaybackBtn = document.getElementById('start-playback-btn');
    const stopPlaybackBtn = document.getElementById('stop-playback-btn');
    
    if (startRecordBtn) startRecordBtn.disabled = isRecording || isPlaying;
    if (stopRecordBtn) stopRecordBtn.disabled = !isRecording;
    if (saveRecordBtn) saveRecordBtn.disabled = isRecording || recordedMovement.movement.length === 0;
    if (loadRecordBtn) loadRecordBtn.disabled = isRecording || isPlaying;
    if (startPlaybackBtn) startPlaybackBtn.disabled = isRecording || isPlaying || recordedMovement.movement.length === 0;
    if (stopPlaybackBtn) stopPlaybackBtn.disabled = !isPlaying;
}

function addRecordingControls() {
    const controlPanel = document.querySelector('.controls');
    if (!controlPanel) return;
    
    // Check if controls already exist
    if (controlPanel.querySelector('.recording-controls')) {
        return;
    }
    
    // Create recording controls container
    const recordingControls = document.createElement('div');
    recordingControls.className = 'recording-controls';
    recordingControls.style.marginTop = '10px';
    
    // Create recording buttons
    const startButton = document.createElement('button');
    startButton.textContent = 'Start Recording';
    startButton.id = 'start-record-btn';
    startButton.onclick = startRecording;
    
    const stopButton = document.createElement('button');
    stopButton.textContent = 'Stop Recording';
    stopButton.id = 'stop-record-btn';
    stopButton.disabled = true;
    stopButton.onclick = stopRecording;
    
    const saveButton = document.createElement('button');
    saveButton.textContent = 'Save Recording';
    saveButton.id = 'save-record-btn';
    saveButton.disabled = true;
    saveButton.onclick = saveRecording;
    
    const loadButton = document.createElement('button');
    loadButton.textContent = 'Load Recording';
    loadButton.id = 'load-record-btn';
    loadButton.onclick = loadRecording;
    
    const playButton = document.createElement('button');
    playButton.textContent = 'Play Recording';
    playButton.id = 'start-playback-btn';
    playButton.disabled = true;
    playButton.onclick = startPlayback;
    
    const stopPlayButton = document.createElement('button');
    stopPlayButton.textContent = 'Stop Playback';
    stopPlayButton.id = 'stop-playback-btn';
    stopPlayButton.disabled = true;
    stopPlayButton.onclick = stopPlayback;
    
    // Add buttons to container
    recordingControls.appendChild(startButton);
    recordingControls.appendChild(stopButton);
    recordingControls.appendChild(saveButton);
    recordingControls.appendChild(loadButton);
    recordingControls.appendChild(playButton);
    recordingControls.appendChild(stopPlayButton);
    
    // Add container to control panel
    controlPanel.appendChild(recordingControls);
}

// Add this new function
async function autoConnectToLastDevice() {
    if (!lastConnectedDeviceId) return;
    
    try {
        // Get all paired HID devices
        const devices = await navigator.hid.getDevices();
        
        // Find our last connected device
        const lastDevice = devices.find(d => 
            `${d.vendorId}-${d.productId}` === lastConnectedDeviceId
        );
        
        if (lastDevice) {
            hidDevice = lastDevice;
            await hidDevice.open();
            
            // Update UI
            statusIndicator.textContent = 'Status: Connected via HID';
            statusIndicator.className = 'status connected';
            connectButton.disabled = true;
            disconnectButton.disabled = false;
            
            // Initialize joint elements
            initializeJointElements();
            
            // Log connection
            addLogMessage(`Auto-connected to HID device: ${hidDevice.productName}`);
            
            // Set up input report handler
            hidDevice.addEventListener('inputreport', handleHIDInput);
        }
    } catch (error) {
        console.error('Auto-connect error:', error);
        addLogMessage('Failed to auto-connect to last device');
    }
}

// Modify the connectToDevice function to store the device ID
async function connectToDevice() {
    try {
        // Request HID device with no filters first to see what's available
        const devices = await navigator.hid.requestDevice({
            filters: [] // Empty filters to see all HID devices
        });

        if (devices.length === 0) {
            throw new Error('No HID device selected');
        }

        // Log device information to help identify the correct IDs
        console.log('Selected device:', {
            vendorId: devices[0].vendorId,
            productId: devices[0].productId,
            productName: devices[0].productName,
            collections: devices[0].collections
        });

        hidDevice = devices[0];
        await hidDevice.open();
        
        // Store the device identifier
        lastConnectedDeviceId = `${hidDevice.vendorId}-${hidDevice.productId}`;
        localStorage.setItem('lastHidDevice', lastConnectedDeviceId);

        // Update UI
        statusIndicator.textContent = 'Status: Connected via HID';
        statusIndicator.className = 'status connected';
        connectButton.disabled = true;
        disconnectButton.disabled = false;
        
        // Initialize joint elements
        initializeJointElements();
        
        // Log connection
        addLogMessage(`Connected to HID device: ${hidDevice.productName}`);
        addLogMessage(`VendorID: 0x${hidDevice.vendorId.toString(16)}, ProductID: 0x${hidDevice.productId.toString(16)}`);

        // Set up input report handler
        hidDevice.addEventListener('inputreport', handleHIDInput);

    } catch (error) {
        console.error('Error connecting to HID device:', error);
        addLogMessage(`Connection error: ${error.message}`);
        
        statusIndicator.textContent = 'Status: Connection failed';
        statusIndicator.className = 'status disconnected';
        connectButton.disabled = false;
        disconnectButton.disabled = true;
        
        hidDevice = null;
    }
}

async function disconnectFromDevice() {
    if (hidDevice) {
        try {
            await hidDevice.close();
            
            // Update UI
            statusIndicator.textContent = 'Status: Disconnected';
            statusIndicator.className = 'status disconnected';
            connectButton.disabled = false;
            disconnectButton.disabled = true;
            
            // Log disconnection
            addLogMessage('Disconnected from HID device');
            
        } catch (error) {
            console.error('Error disconnecting:', error);
            addLogMessage(`Disconnection error: ${error.message}`);
        }
        
        hidDevice = null;
    }
}

let firstFrame = true;

// Update quaternion to Euler conversion to match MCU implementation
function quaternionToEuler(x, y, z, w) {
    // Calculate squared terms
    const sqw = w * w;
    const sqx = x * x;
    const sqy = y * y;
    const sqz = z * z;

    // Calculate Euler angles (in radians)
    const yaw = Math.asin(-2.0 * (x * z - y * w) /
                         (sqx + sqy + sqz + sqw));
    
    const pitch = Math.atan2(2.0 * (x * y + z * w),
                            (sqx - sqy - sqz + sqw));
    
    const roll = Math.atan2(2.0 * (y * z + x * w),
                           (-sqx - sqy + sqz + sqw));

    return { roll, pitch, yaw };
}

// Modify the handleHIDInput function to adjust roll by 180 degrees
function handleHIDInput(event) {
    if (ignoreExternalInput) return;

    const { data } = event;
    if (data.getUint8(0) !== REPORT_ID) return;

    let hasChanges = false;
    
    // Process first 16 axes
    for (let i = 0; i < 16; i++) {
        const rawValue = data.getUint8(i + 3);
        let finalValue = rawValue;
        
        if (fingerJointMap[i]?.inverted) {
            if (fingerJointMap[i].type.includes('ABDUCTION')) {
                finalValue = 255 - rawValue;
            } else {
                const min = fingerJointMap[i].min;
                const max = fingerJointMap[i].max;
                finalValue = max - (rawValue - min);
            }
        }
        
        if (jointValues[i] !== finalValue) {
            // console.log(`Joint ${i} (${fingerJointMap[i]?.type}): ${jointValues[i]} -> ${finalValue}`);
        jointValues[i] = finalValue;
        updateJointDisplay(i, finalValue);
            hasChanges = true;
        }
    }

    // Process quaternion values
    const quaternionX = (data.getUint8(19) - 127) / 127;
    const quaternionY = (data.getUint8(20) - 127) / 127;
    const quaternionZ = (data.getUint8(21) - 127) / 127;
    const quaternionW = (data.getUint8(22) - 127) / 127;
    const euler = quaternionToEuler(quaternionX, quaternionY, quaternionZ, quaternionW);

    const roll = Math.PI - (euler.roll);
    const pitch = Math.PI - (euler.pitch + Math.PI);
    const yaw = euler.yaw + Math.PI;

    const linearX = data.getUint8(24);
    const linearY = data.getUint8(23);
    const linearZ = data.getUint8(25);

    const positionScale = 0.1; // Adjust this value to change movement sensitivity
    const centerOffset = 128; // 0x80

    if (hand.palm) {
        // Apply rotations as before
        hand.palm.rotation.x = pitch;
        hand.palm.rotation.y = yaw;
        hand.palm.rotation.z = roll;

        // Calculate offset from center (128) for each axis
        // Positive values mean right/up/forward, negative values mean left/down/back
        const moveX = (linearX - centerOffset) * positionScale;
        const moveY = (linearY - centerOffset) * positionScale;
        const moveZ = (linearZ - centerOffset) * positionScale;

        // Only move if the value is different from center (allowing for small deadzone)
        const deadzone = 1; // Adjust this value to change deadzone size
        if (Math.abs(linearX - centerOffset) > deadzone) {
            hand.palm.position.x += moveX;
        }
        if (Math.abs(linearY - centerOffset) > deadzone) {
            hand.palm.position.y += moveY;
        }
        if (Math.abs(linearZ - centerOffset) > deadzone) {
            hand.palm.position.z += moveZ;
        }

        hand.palm.updateMatrixWorld(true);
    }

    // Update the display with the raw quaternion values
    updateQuaternionDisplay(quaternionX, quaternionY, quaternionZ, quaternionW);

    if (hasChanges) {
        updateHandModel();
    }
}

// Add log message function (needs to be defined early)
function addLogMessage(message) {
    const logEntry = document.createElement('div');
    logEntry.textContent = message;
    logContainer.appendChild(logEntry);
    logContainer.scrollTop = logContainer.scrollHeight;
    
    // Limit log entries
    while (logContainer.children.length > 100) {
        logContainer.removeChild(logContainer.firstChild);
    }
}

// Initialize joint elements in sidebar
function initializeJointElements() {
    jointsContainer.innerHTML = '';
    
    for (let i = 0; i < MAX_JOINTS; i++) {
        const jointElement = document.createElement('div');
        jointElement.className = 'joint-info';
        
        // Get finger and joint info
        const fingerIndex = i < 4 ? 0 : Math.floor((i - 4) / 3) + 1;
        const jointType = fingerJointMap[i]?.type || 'Unknown';
        const fingerName = ['Thumb', 'Index', 'Middle', 'Ring', 'Pinky'][fingerIndex];
        
        jointElement.innerHTML = `
            <div class="joint-name">${fingerName} - ${jointType}</div>
            <div class="joint-value" id="joint-value-${i}">Value: 0</div>
            <div class="bar-container">
                <div class="bar" id="joint-bar-${i}"></div>
            </div>
            <label class="invert-toggle">
                <input type="checkbox" id="invert-${i}" ${fingerJointMap[i]?.inverted ? 'checked' : ''}>
                Invert Values
            </label>
        `;
        jointsContainer.appendChild(jointElement);
        
        // Add event listener for the invert checkbox
        const invertCheckbox = document.getElementById(`invert-${i}`);
        invertCheckbox.addEventListener('change', (e) => {
            if (i < fingerJointMap.length) {
                fingerJointMap[i].inverted = e.target.checked;
                addLogMessage(`${fingerName} ${jointType} inversion ${e.target.checked ? 'enabled' : 'disabled'}`);
            }
        });
    }
    
    // Modify quaternion element to include Euler angles
    const quaternionElement = document.createElement('div');
    quaternionElement.className = 'joint-info';
    quaternionElement.innerHTML = `
        <div class="joint-name">Orientation</div>
        <div class="quaternion-values">
            <div>X: <span id="quat-x">0.000</span></div>
            <div>Y: <span id="quat-y">0.000</span></div>
            <div>Z: <span id="quat-z">0.000</span></div>
            <div>W: <span id="quat-w">0.000</span></div>
        </div>
        <div class="euler-values">
            <div>Roll: <span id="euler-roll">0.0°</span></div>
            <div>Pitch: <span id="euler-pitch">0.0°</span></div>
            <div>Yaw: <span id="euler-yaw">0.0°</span></div>
        </div>
        <div class="bar-container">
            <div class="quaternion-bars">
                <div class="bar" id="quat-bar-x"></div>
                <div class="bar" id="quat-bar-y"></div>
                <div class="bar" id="quat-bar-z"></div>
                <div class="bar" id="quat-bar-w"></div>
            </div>
        </div>
    `;
    jointsContainer.appendChild(quaternionElement);
}

// Update joint display in sidebar
function updateJointDisplay(jointIndex, value) {
    const valueElement = document.getElementById(`joint-value-${jointIndex}`);
    const barElement = document.getElementById(`joint-bar-${jointIndex}`);
    
    if (valueElement && barElement) {
        valueElement.textContent = `Value: ${value}`;
        
        // Calculate percentage based on joint's min/max values
        const jointInfo = fingerJointMap[jointIndex];
        const min = jointInfo?.min || 0;
        const max = jointInfo?.max || 255;
        const range = max - min;
        
        const percentage = Math.min(100, Math.max(0, ((value - min) / range) * 100));
        barElement.style.width = `${percentage}%`;
        
        // Change color based on value
        const hue = Math.floor(percentage * 1.2); // 0-120 (red to green)
        barElement.style.backgroundColor = `hsl(${hue}, 80%, 50%)`;
    }
}

// Update the quaternion display function to show Euler angles
function updateQuaternionDisplay(x, y, z, w) {
    // Update quaternion values
    document.getElementById('quat-x').textContent = x.toFixed(3);
    document.getElementById('quat-y').textContent = y.toFixed(3);
    document.getElementById('quat-z').textContent = z.toFixed(3);
    document.getElementById('quat-w').textContent = w.toFixed(3);
    
    // Calculate and update Euler angles
    const euler = quaternionToEuler(x, y, z, w);
    document.getElementById('euler-roll').textContent = `${(euler.roll * 180 / Math.PI).toFixed(1)}°`;
    document.getElementById('euler-pitch').textContent = `${(euler.pitch * 180 / Math.PI).toFixed(1)}°`;
    document.getElementById('euler-yaw').textContent = `${(euler.yaw * 180 / Math.PI).toFixed(1)}°`;
    
    // Update bars
    const updateBar = (id, value) => {
        const bar = document.getElementById(id);
        if (bar) {
            const percentage = ((value + 1) / 2) * 100;
            bar.style.width = `${percentage}%`;
            const hue = value >= 0 ? 120 : 0;
            const saturation = Math.abs(value) * 100;
            bar.style.backgroundColor = `hsl(${hue}, ${saturation}%, 50%)`;
        }
    };
    
    updateBar('quat-bar-x', x);
    updateBar('quat-bar-y', y);
    updateBar('quat-bar-z', z);
    updateBar('quat-bar-w', w);
}

// Add additional styles for Euler angles display
const additionalStyles = `
.quaternion-values, .euler-values {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 10px;
    margin: 5px 0;
    font-family: monospace;
}

.euler-values {
    grid-template-columns: repeat(3, 1fr);
    color: #666;
}

.quaternion-bars {
    display: grid;
    grid-template-rows: repeat(4, 1fr);
    gap: 2px;
}

.quaternion-bars .bar {
    height: 10px;
    transition: all 0.1s ease;
}
`;

// Add the new styles to the existing styleElement
styleElement.textContent += additionalStyles;

// Add to the end of the file or where other initialization code is
// Try to auto-connect when the page loads
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', autoConnectToLastDevice);
} else {
    autoConnectToLastDevice();
}
