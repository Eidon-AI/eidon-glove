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
let hidDevices = new Map(); // Using Map to store devices by their ID
const REPORT_ID = 1;
const GLOVE_REPORT_SIZE = 24;
const TRACKER_REPORT_SIZE = 3;
const trackers = new Map(); // Map to store tracker data by deviceId

// Add at the start of the file, with other global variables
let lastConnectedDeviceId = localStorage.getItem('lastHidDevice');

// Add these variables at the top of the file with other globals
let lastLinearX = 128;
let lastLinearY = 128;
let lastLinearZ = 128;

// Add to the top with other global variables
let compassElement = null;

// Add at the top with other global variables
const gloves = new Map(); // Map to store glove data by deviceId

// Add to global variables
const hands = new Map(); // Map to store hand models by deviceId

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
    controls.enabled = true;
    
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
    
    // Handle window resize
    window.addEventListener('resize', onWindowResize);
    
    // Start animation loop
    animate();
}

// Modify createHandModel to create a hand for a specific device
function createHandModel(deviceId) {
    const handModel = {
        palm: null,
        fingers: []
    };

    // Create materials
    const palmMaterial = new THREE.MeshPhongMaterial({ color: 0xf5c396 });
    const fingerMaterial = new THREE.MeshPhongMaterial({ color: 0xf5c396 });
    const jointMaterial = new THREE.MeshPhongMaterial({ color: 0xe3a977 });
    
    // Create palm
    const palmGeometry = new THREE.BoxGeometry(6, 1.25, 7);
    handModel.palm = new THREE.Mesh(palmGeometry, palmMaterial);
    handModel.palm.position.set(0, 4, 0);
    handModel.palm.rotation.x = Math.PI;
    
    // Offset each hand model so they don't overlap
    const handCount = hands.size;
    handModel.palm.position.x = handCount * 8 + 8; // Space hands horizontally
    
    scene.add(handModel.palm);
    
    // Finger dimensions
    const fingerWidth = 1;
    const fingerHeight = 0.8;
    const fingerSegmentLengths = [3, 2, 1.5];
    const thumbSegmentLengths = [3, 2, 1.5];
    
    const fingerBasePositions = [
        [3, 0, 0],    // Thumb
        [2.5, -0.5, -3.5],  // Index
        [0.83, -0.5, -3.5], // Middle
        [-0.83, -0.5, -3.5],// Ring
        [-2.5, -0.5, -3.5]  // Pinky
    ];
    
    // Create fingers with direct rotation groups
    handModel.fingers = [];
    
    for (let f = 0; f < 5; f++) {
        const finger = {
            name: ['Thumb', 'Index', 'Middle', 'Ring', 'Pinky'][f],
            base: new THREE.Group(), // Base group for finger position
            rotationGroups: [], // Store rotation groups directly
            segments: []
        };
        
        // Set finger base position
        finger.base.position.set(...fingerBasePositions[f]);
        handModel.palm.add(finger.base);
        
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
        
        handModel.fingers.push(finger);
    }
    
    // Add labels
    addFingerLabels(handModel);
    addHandLabel(handModel);

    // Store the hand model in the hands Map
    hands.set(deviceId, handModel);
    return handModel;
}

// Function to add finger labels
function addFingerLabels(handModel) {
        const fingerNames = ['Thumb', 'Index', 'Middle', 'Ring', 'Pinky'];
        
        for (let i = 0; i < handModel.fingers.length; i++) {
            const finger = handModel.fingers[i];
            
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
function addHandLabel(handModel) {
    // Create a canvas element
    const canvas = document.createElement('canvas');
    // const context = canvas.getContext('2d');
    canvas.width = 256;
    canvas.height = 64;
    
    // Draw text on the canvas
    // context.fillStyle = '#ffffff';
    // context.fillRect(0, 0, canvas.width, canvas.height);
    // context.font = 'Bold 24px Arial';
    // context.fillStyle = '#000000';
    // context.textAlign = 'center';
    // context.textBaseline = 'middle';
    // context.fillText('RIGHT HAND (PALM UP)', canvas.width / 2, canvas.height / 2);
    
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

// Modify disconnectFromDevice to clean up UI and 3D elements
async function disconnectFromDevice(deviceId = null) {
    const savedDevices = JSON.parse(localStorage.getItem('hidDevices') || '[]');

    if (deviceId) {
        // Disconnect specific device
        const device = hidDevices.get(deviceId);
        if (device) {
            await device.close();
            hidDevices.delete(deviceId);
            
            // Remove from localStorage
            const updatedDevices = savedDevices.filter(id => id !== deviceId);
            localStorage.setItem('hidDevices', JSON.stringify(updatedDevices));
            
            // Clean up UI and 3D elements
            cleanupDevice(deviceId);
            
            addLogMessage(`Disconnected from HID device: ${device.productName}`);
        }
    } else {
        // Disconnect all devices
        for (const [id, device] of hidDevices) {
            await device.close();
            cleanupDevice(id);
            addLogMessage(`Disconnected from HID device: ${device.productName}`);
        }
        hidDevices.clear();
        localStorage.setItem('hidDevices', '[]');
    }
    
    updateConnectionStatus();
}

// Add function to clean up device-specific elements
function cleanupDevice(deviceId) {
    // Remove tracker UI and data if it's a tracker
    if (trackers.has(deviceId)) {
        const trackerElement = document.getElementById(`tracker-${deviceId}`);
        if (trackerElement) {
            trackerElement.remove();
        }
        trackers.delete(deviceId);
    }

    // Remove glove UI, data, and 3D model if it's a glove
    if (gloves.has(deviceId)) {
        // Remove UI
        const gloveElement = document.getElementById(`glove-${deviceId}`);
        if (gloveElement) {
            gloveElement.remove();
        }
        
        // Remove 3D model
        const handModel = hands.get(deviceId);
        if (handModel) {
            // Remove palm
            if (handModel.palm) {
                scene.remove(handModel.palm);
            }
            
            // Remove any other Three.js objects associated with this hand
            // This ensures we don't leave any orphaned objects in the scene
            handModel.fingers.forEach(finger => {
                if (finger.base) {
                    handModel.palm.remove(finger.base);
                }
            });
        }
        
        // Clear from Maps
        gloves.delete(deviceId);
        hands.delete(deviceId);
        
        // Reposition remaining hands
        repositionHands();
    }
}

// Add function to reposition hands after a disconnect
function repositionHands() {
    let index = 0;
    for (const [deviceId, handModel] of hands) {
        // Smoothly animate to new position
        const targetX = index * 8 - 4;
        animateHandPosition(handModel, targetX);
        index++;
    }
}

// Add function to smoothly animate hand position changes
function animateHandPosition(handModel, targetX) {
    const startX = handModel.palm.position.x;
    const duration = 1000; // 1 second animation
    const startTime = Date.now();

    function update() {
        const elapsed = Date.now() - startTime;
        const progress = Math.min(elapsed / duration, 1);
        
        // Use easing function for smooth movement
        const easeProgress = progress * (2 - progress);
        
        handModel.palm.position.x = startX + (targetX - startX) * easeProgress;
        
        if (progress < 1) {
            requestAnimationFrame(update);
        }
    }

    requestAnimationFrame(update);
}

// Modify updateHandModel to handle multiple hands
function updateHandModel(deviceId) {
    if (!scene || !camera || !renderer) return;
    
    const handModel = hands.get(deviceId);
    const gloveData = gloves.get(deviceId);
    
    if (!handModel || !gloveData) return;
    
    // Process each joint
    for (let i = 0; i < MAX_JOINTS; i++) {
        const jointInfo = fingerJointMap[i];
        if (!jointInfo) continue;
        
        const { finger, type, min, max } = jointInfo;
        const value = gloveData.jointValues[i];
        const currentFinger = handModel.fingers[finger];
        
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
                    currentFinger.base.rotation.z = Math.PI / 4 - (angle * 0.75);
                    currentFinger.base.rotation.y = -Math.PI / 2 - (angle * 0.25);
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
                    // Add proportional rotation to the DIP joint (last joint)
                    if (currentFinger.rotationGroups[2]) {
                        // DIP typically bends about 1.3x the PIP angle
                        currentFinger.rotationGroups[2].rotation.x = angle * 0.6;
                    }
                    break;
            }
        }
    }
    
    // Apply quaternion rotations
    const euler = gloveData.euler;
    const roll = Math.PI - (euler.roll);
    const pitch = Math.PI - (euler.pitch + Math.PI);
    const yaw = euler.yaw + Math.PI;

    handModel.palm.rotation.x = pitch;
    handModel.palm.rotation.y = yaw;
    handModel.palm.rotation.z = roll;
    
    handModel.palm.updateMatrixWorld(true);
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
disconnectButton.addEventListener('click', () => {
    // Disconnect all devices
    disconnectFromDevice();
});

// Check if Web HID API is supported
if (!navigator.hid) {
    statusIndicator.textContent = 'Status: WebHID API not supported in this browser';
    connectButton.disabled = true;
    addLogMessage('ERROR: WebHID API is not supported in this browser. Try Chrome or Edge.');
}

// Initialize Three.js scene
initThreeJS();

// Initialize joint elements
// initializeJointElements();

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

.device-item {
    margin: 5px 0;
    padding: 5px;
    border: 1px solid #ccc;
    border-radius: 4px;
    display: flex;
    justify-content: space-between;
    align-items: center;
}

.device-item button {
    padding: 2px 8px;
    background: #ff4444;
    color: white;
    border: none;
    border-radius: 3px;
    cursor: pointer;
}

.device-item button:hover {
    background: #cc0000;
}

.device-details {
    font-size: 0.8em;
    color: #666;
    margin-left: 10px;
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

// Modify the connectToDevice function to handle identical devices
async function connectToDevice() {
    try {
        const devices = await navigator.hid.requestDevice({
            filters: [] // Empty filters to see all HID devices
        });

        for (const device of devices) {
            // Create a unique device ID by combining vendorId, productId, and the device index
            const baseDeviceId = `${device.vendorId}-${device.productId}-${device.productName.replace(/\s+/g, '-')}`;
            let deviceId = baseDeviceId;
            let index = 1;

            // If a device with this ID already exists, increment index until we find a unique ID
            while (hidDevices.has(deviceId)) {
                deviceId = `${baseDeviceId}-${index}`;
                index++;
            }
            
            await device.open();
            hidDevices.set(deviceId, device);
            
            // Store in localStorage (as array of IDs)
            const savedDevices = JSON.parse(localStorage.getItem('hidDevices') || '[]');
            if (!savedDevices.includes(deviceId)) {
                savedDevices.push(deviceId);
                localStorage.setItem('hidDevices', JSON.stringify(savedDevices));
            }

            // Set up input report handler for this device
            device.addEventListener('inputreport', handleHIDInput);
            
            addLogMessage(`Connected to HID device: ${device.productName} (${deviceId})`);
            addLogMessage(`VendorID: 0x${device.vendorId.toString(16)}, ProductID: 0x${device.productId.toString(16)}`);
        }

        // Update UI
        updateConnectionStatus();

    } catch (error) {
        console.error('Error connecting to HID device:', error);
        addLogMessage(`Connection error: ${error.message}`);
    }
}

// Update autoConnectToLastDevice to handle the new ID format
async function autoConnectToLastDevice() {
    const savedDevices = JSON.parse(localStorage.getItem('hidDevices') || '[]');
    if (savedDevices.length === 0) return;
    
    try {
        const devices = await navigator.hid.getDevices();
        
        for (const deviceId of savedDevices) {
            // Extract the base device ID (vendorId-productId) from the saved device ID
            const [vendorId, productId] = deviceId.split('-').slice(0, 2);
            const device = devices.find(d => 
                d.vendorId.toString() === vendorId && 
                d.productId.toString() === productId &&
                !Array.from(hidDevices.values()).includes(d)
            );
            
            if (device && !hidDevices.has(deviceId)) {
                await device.open();
                hidDevices.set(deviceId, device);
                device.addEventListener('inputreport', handleHIDInput);
                addLogMessage(`Auto-connected to HID device: ${device.productName} (${deviceId})`);
            }
        }
        
        updateConnectionStatus();
            
    } catch (error) {
        console.error('Auto-connect error:', error);
        addLogMessage('Failed to auto-connect to saved devices');
    }
}

// Update updateConnectionStatus to show more device details
function updateConnectionStatus() {
    if (hidDevices.size > 0) {
        statusIndicator.textContent = `Status: Connected to ${hidDevices.size} device(s)`;
        statusIndicator.className = 'status connected';
        connectButton.disabled = false;
        disconnectButton.disabled = false;

        let deviceList = document.getElementById('device-list');
        if (!deviceList) {
            deviceList = document.createElement('div');
            deviceList.id = 'device-list';
            statusIndicator.parentNode.insertBefore(deviceList, statusIndicator.nextSibling);
        }
        deviceList.innerHTML = '';

        for (const [deviceId, device] of hidDevices) {
            const deviceDiv = document.createElement('div');
            deviceDiv.className = 'device-item';
            deviceDiv.innerHTML = `
                ${device.productName} 
                <span class="device-details">
                    (ID: ${deviceId})
                </span>
                <button onclick="disconnectFromDevice('${deviceId}')">Disconnect</button>
            `;
            deviceList.appendChild(deviceDiv);
        }
    } else {
            statusIndicator.textContent = 'Status: Disconnected';
            statusIndicator.className = 'status disconnected';
            connectButton.disabled = false;
            disconnectButton.disabled = true;
            
        const deviceList = document.getElementById('device-list');
        if (deviceList) {
            deviceList.remove();
        }
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

// Modify handleHIDInput to use the new multi-hand system
function handleHIDInput(event) {
    if (ignoreExternalInput) return;

    const device = event.device;
    const deviceId = `${device.vendorId}-${device.productId}-${device.productName.replace(/\s+/g, '-')}`;
    const { data } = event;
    
    // if (data.getUint8(0) !== REPORT_ID) return;

    // Determine if this is a tracker or glove based on report size
    const isTracker = data.buffer.byteLength === TRACKER_REPORT_SIZE;

    if (isTracker) {
        // Handle tracker data
        const roll = data.getUint8(0) * (360/255); // Convert to degrees (0-360)
        const pitch = data.getUint8(1) * (360/255);
        const yaw = data.getUint8(2) * (360/255);
        
        // Create display if it doesn't exist
        if (!trackers.has(deviceId)) {
            addTrackerDisplay(deviceId);
        }
        
        // Update tracker display
        updateTrackerDisplay(deviceId, roll, pitch, yaw);
        
    } else {
        // Glove handling
        if (!gloves.has(deviceId)) {
            addGloveDisplay(deviceId);
            createHandModel(deviceId); // Create 3D hand model for this device
        }

        const gloveData = gloves.get(deviceId);
        let hasChanges = false;
        
        // Process joint values
        for (let i = 0; i < 16; i++) {
            const rawValue = data.getUint8(i + 3);
            let finalValue = rawValue;
            
            if (gloveData.jointInversions[i]) {
                if (fingerJointMap[i].type.includes('ABDUCTION')) {
                    finalValue = 255 - rawValue;
                } else {
                    const min = fingerJointMap[i].min;
                    const max = fingerJointMap[i].max;
                    finalValue = max - (rawValue - min);
                }
            }
            
            if (gloveData.jointValues[i] !== finalValue) {
                gloveData.jointValues[i] = finalValue;
                updateJointDisplay(deviceId, i, finalValue);
                hasChanges = true;
            }
        }

        // Process quaternion values
        const quaternionX = (data.getUint8(19) - 127) / 127;
        const quaternionY = (data.getUint8(20) - 127) / 127;
        const quaternionZ = (data.getUint8(21) - 127) / 127;
        const quaternionW = (data.getUint8(22) - 127) / 127;
        
        gloveData.quaternion = { x: quaternionX, y: quaternionY, z: quaternionZ, w: quaternionW };
        const euler = quaternionToEuler(quaternionX, quaternionY, quaternionZ, quaternionW);
        gloveData.euler = euler;

        // Update displays
        updateQuaternionDisplay(deviceId, quaternionX, quaternionY, quaternionZ, quaternionW);

        // Update this specific hand model
        if (hasChanges) {
            updateHandModel(deviceId);
        }
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
            <div>Roll:<br><span id="euler-roll">0.0°</span></div>
            <div>Pitch:<br><span id="euler-pitch">0.0°</span></div>
            <div>Yaw:<br><span id="euler-yaw">0.0°</span></div>
        </div>
        <div class="quaternion-bars">
            <div class="bar-container">
                <div class="bar" id="quat-bar-x"></div>
            </div>
            <div class="bar-container">
                <div class="bar" id="quat-bar-y"></div>
            </div>
            <div class="bar-container">
                <div class="bar" id="quat-bar-z"></div>
            </div>
            <div class="bar-container">
                <div class="bar" id="quat-bar-w"></div>
            </div>
        </div>
    `;
    jointsContainer.appendChild(quaternionElement);
}

// Update joint display in sidebar
function updateJointDisplay(deviceId, jointIndex, value) {
    const valueElement = document.getElementById(`joint-value-${deviceId}-${jointIndex}`);
    const barElement = document.getElementById(`joint-bar-${deviceId}-${jointIndex}`);
    
    if (valueElement && barElement) {
        valueElement.textContent = `Value: ${value}`;
        
        const jointInfo = fingerJointMap[jointIndex];
        const min = jointInfo?.min || 0;
        const max = jointInfo?.max || 255;
        const range = max - min;
        
        const percentage = Math.min(100, Math.max(0, ((value - min) / range) * 100));
        barElement.style.width = `${percentage}%`;
        
        const hue = Math.floor(percentage * 1.2);
        barElement.style.backgroundColor = `hsl(${hue}, 80%, 50%)`;
    }
}

// Update the quaternion display function to show Euler angles
function updateQuaternionDisplay(deviceId, x, y, z, w) {
    // Update quaternion values
    document.getElementById(`quat-x-${deviceId}`).textContent = x.toFixed(3);
    document.getElementById(`quat-y-${deviceId}`).textContent = y.toFixed(3);
    document.getElementById(`quat-z-${deviceId}`).textContent = z.toFixed(3);
    document.getElementById(`quat-w-${deviceId}`).textContent = w.toFixed(3);
    
    // Calculate and update Euler angles
    const euler = quaternionToEuler(x, y, z, w);
    document.getElementById(`euler-roll-${deviceId}`).textContent = `${(euler.roll * 180 / Math.PI).toFixed(1)}°`;
    document.getElementById(`euler-pitch-${deviceId}`).textContent = `${(euler.pitch * 180 / Math.PI).toFixed(1)}°`;
    document.getElementById(`euler-yaw-${deviceId}`).textContent = `${(euler.yaw * 180 / Math.PI).toFixed(1)}°`;
    
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
    
    updateBar(`quat-bar-x-${deviceId}`, x);
    updateBar(`quat-bar-y-${deviceId}`, y);
    updateBar(`quat-bar-z-${deviceId}`, z);
    updateBar(`quat-bar-w-${deviceId}`, w);
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

// Add this function to create and add the compass
function addCompassOverlay() {
    // Create compass container
    compassElement = document.createElement('div');
    compassElement.style.cssText = `
        position: fixed;
        top: 80px;
        left: 20px;
        width: 100px;
        height: 100px;
        border-radius: 50%;
        background: rgba(255, 255, 255, 0.9);
        border: 2px solid #333;
        box-shadow: 0 0 10px rgba(0,0,0,0.2);
        display: flex;
        justify-content: center;
        align-items: center;
    `;

    // Add fixed cardinal direction markers
    const directions = ['N', 'E', 'S', 'W'];
    const directionContainer = document.createElement('div');
    directionContainer.style.cssText = `
        position: absolute;
        width: 100%;
        height: 100%;
    `;

    directions.forEach((dir, i) => {
        const marker = document.createElement('div');
        marker.style.cssText = `
            position: absolute;
            left: 50%;
            top: 50%;
            font-weight: bold;
            transform-origin: 0 0;
        `;
        
        // Position each marker
        switch(dir) {
            case 'N': 
                marker.style.transform = 'translate(-50%, -40px)';
                break;
            case 'E':
                marker.style.transform = 'translate(20px, -50%)';
                break;
            case 'S':
                marker.style.transform = 'translate(-50%, 25px)';
                break;
            case 'W':
                marker.style.transform = 'translate(-40px, -50%)';
                break;
        }
        
        marker.textContent = dir;
        directionContainer.appendChild(marker);
    });

    // Create compass needle
    const needle = document.createElement('div');
    needle.style.cssText = `
        position: absolute;
        width: 4px;
        height: 50px;
        background: linear-gradient(to bottom, red 50%, #333 50%);
        transform-origin: center center;
    `;

    compassElement.appendChild(directionContainer);
    compassElement.appendChild(needle);
    document.body.appendChild(compassElement);
}

// Add this function to create the tracker display section
function addTrackerDisplay(deviceId) {
    console.log(`added: ${deviceId}`);
    const trackerId = deviceId.split('-').pop(); // Get unique part of device ID
    const trackerElement = document.createElement('div');
    trackerElement.className = 'tracker-info';
    trackerElement.id = `tracker-${deviceId}`;
    
    trackerElement.innerHTML = `
        <div class="tracker-name">Tracker ${trackerId}</div>
        <div class="tracker-values">
            <div>Roll:<br><span id="tracker-roll-${deviceId}">0.0°</span></div>
            <div>Pitch:<br><span id="tracker-pitch-${deviceId}">0.0°</span></div>
            <div>Yaw:<br><span id="tracker-yaw-${deviceId}">0.0°</span></div>
        </div>
        <div class="tracker-bars">
            <div class="bar-container">
                <div class="bar" id="tracker-bar-roll-${deviceId}"></div>
            </div>
            <div class="bar-container">
                <div class="bar" id="tracker-bar-pitch-${deviceId}"></div>
            </div>
            <div class="bar-container">
                <div class="bar" id="tracker-bar-yaw-${deviceId}"></div>
            </div>
        </div>
    `;
    
    // Add to joints container after the quaternion display
    jointsContainer.appendChild(trackerElement);
    
    // Add to trackers Map
    trackers.set(deviceId, {
        roll: 0,
        pitch: 0,
        yaw: 0
    });
}

// Add this function to update tracker display
function updateTrackerDisplay(deviceId, roll, pitch, yaw) {
    // Update stored values
    trackers.set(deviceId, { roll, pitch, yaw });
    
    // Update display values
    document.getElementById(`tracker-roll-${deviceId}`).textContent = `${roll.toFixed(1)}°`;
    document.getElementById(`tracker-pitch-${deviceId}`).textContent = `${pitch.toFixed(1)}°`;
    document.getElementById(`tracker-yaw-${deviceId}`).textContent = `${yaw.toFixed(1)}°`;
    
    // Update bars
    const updateBar = (id, value) => {
        const bar = document.getElementById(id);
        if (bar) {
            // Normalize value from 0-360 to 0-100 for bar display
            const percentage = (value % 360) / 3.6;
            bar.style.width = `${percentage}%`;
            const hue = percentage * 1.2; // 0-120 (red to green)
            bar.style.backgroundColor = `hsl(${hue}, 80%, 50%)`;
        }
    };
    
    updateBar(`tracker-bar-roll-${deviceId}`, roll);
    updateBar(`tracker-bar-pitch-${deviceId}`, pitch);
    updateBar(`tracker-bar-yaw-${deviceId}`, yaw);
}

// Add this function to create a glove display section
function addGloveDisplay(deviceId) {
    const gloveId = deviceId.split('-').pop(); // Get unique part of device ID
    const gloveElement = document.createElement('div');
    gloveElement.className = 'glove-info';
    gloveElement.id = `glove-${deviceId}`;
    
    // Create glove header
    const header = document.createElement('div');
    header.className = 'glove-header';
    header.textContent = `Glove ${gloveId}`;
    gloveElement.appendChild(header);

    // Create joints container for this glove
    const glovejointsContainer = document.createElement('div');
    glovejointsContainer.className = 'glove-joints-container';
    
    // Create joint elements for this glove
    for (let i = 0; i < MAX_JOINTS; i++) {
        const jointElement = document.createElement('div');
        jointElement.className = 'joint-info';
        
        const fingerIndex = i < 4 ? 0 : Math.floor((i - 4) / 3) + 1;
        const jointType = fingerJointMap[i]?.type || 'Unknown';
        const fingerName = ['Thumb', 'Index', 'Middle', 'Ring', 'Pinky'][fingerIndex];
        
        jointElement.innerHTML = `
            <div class="joint-name">${fingerName} - ${jointType}</div>
            <div class="joint-value" id="joint-value-${deviceId}-${i}">Value: 0</div>
            <div class="bar-container">
                <div class="bar" id="joint-bar-${deviceId}-${i}"></div>
            </div>
            <label class="invert-toggle">
                <input type="checkbox" id="invert-${deviceId}-${i}" ${fingerJointMap[i]?.inverted ? 'checked' : ''}>
                Invert Values
            </label>
        `;
        glovejointsContainer.appendChild(jointElement);
        
        // Add event listener for the invert checkbox
        const invertCheckbox = document.getElementById(`invert-${deviceId}-${i}`);
        // invertCheckbox.addEventListener('change', (e) => {
        //     if (i < fingerJointMap.length) {
        //         // Store inversion state per device and joint
        //         const gloveData = gloves.get(deviceId);
        //         if (gloveData) {
        //             gloveData.jointInversions[i] = e.target.checked;
        //         }
        //         addLogMessage(`Glove ${gloveId} ${fingerName} ${jointType} inversion ${e.target.checked ? 'enabled' : 'disabled'}`);
        //     }
        // });
    }

    // Add quaternion display for this glove
    const quaternionElement = document.createElement('div');
    quaternionElement.className = 'joint-info';
    quaternionElement.innerHTML = `
        <div class="joint-name">Orientation</div>
        <div class="quaternion-values">
            <div>X: <span id="quat-x-${deviceId}">0.000</span></div>
            <div>Y: <span id="quat-y-${deviceId}">0.000</span></div>
            <div>Z: <span id="quat-z-${deviceId}">0.000</span></div>
            <div>W: <span id="quat-w-${deviceId}">0.000</span></div>
        </div>
        <div class="euler-values">
            <div>Roll: <span id="euler-roll-${deviceId}">0.0°</span></div>
            <div>Pitch: <span id="euler-pitch-${deviceId}">0.0°</span></div>
            <div>Yaw: <span id="euler-yaw-${deviceId}">0.0°</span></div>
        </div>
        <div class="quaternion-bars">
            <div class="bar-container">
                <div class="bar" id="quat-bar-x-${deviceId}"></div>
            </div>
            <div class="bar-container">
                <div class="bar" id="quat-bar-y-${deviceId}"></div>
            </div>
            <div class="bar-container">
                <div class="bar" id="quat-bar-z-${deviceId}"></div>
            </div>
            <div class="bar-container">
                <div class="bar" id="quat-bar-w-${deviceId}"></div>
            </div>
        </div>
    `;
    glovejointsContainer.appendChild(quaternionElement);
    gloveElement.appendChild(glovejointsContainer);
    
    // Add to joints container
    jointsContainer.appendChild(gloveElement);
    
    // Initialize glove data in the Map
    gloves.set(deviceId, {
        jointValues: new Array(MAX_JOINTS).fill(0),
        jointInversions: new Array(MAX_JOINTS).fill(false),
        quaternion: { x: 0, y: 0, z: 0, w: 1 },
        euler: { roll: 0, pitch: 0, yaw: 0 }
    });
}

// Update the joint display function to handle multiple gloves
function updateJointDisplay(deviceId, jointIndex, value) {
    const valueElement = document.getElementById(`joint-value-${deviceId}-${jointIndex}`);
    const barElement = document.getElementById(`joint-bar-${deviceId}-${jointIndex}`);
    
    if (valueElement && barElement) {
        valueElement.textContent = `Value: ${value}`;
        
        const jointInfo = fingerJointMap[jointIndex];
        const min = jointInfo?.min || 0;
        const max = jointInfo?.max || 255;
        const range = max - min;
        
        const percentage = Math.min(100, Math.max(0, ((value - min) / range) * 100));
        barElement.style.width = `${percentage}%`;
        
        const hue = Math.floor(percentage * 1.2);
        barElement.style.backgroundColor = `hsl(${hue}, 80%, 50%)`;
    }
}

// Update the quaternion display function to handle multiple gloves
function updateQuaternionDisplay(deviceId, x, y, z, w) {
    // Update quaternion values
    document.getElementById(`quat-x-${deviceId}`).textContent = x.toFixed(3);
    document.getElementById(`quat-y-${deviceId}`).textContent = y.toFixed(3);
    document.getElementById(`quat-z-${deviceId}`).textContent = z.toFixed(3);
    document.getElementById(`quat-w-${deviceId}`).textContent = w.toFixed(3);
    
    // Calculate and update Euler angles
    const euler = quaternionToEuler(x, y, z, w);
    document.getElementById(`euler-roll-${deviceId}`).textContent = `${(euler.roll * 180 / Math.PI).toFixed(1)}°`;
    document.getElementById(`euler-pitch-${deviceId}`).textContent = `${(euler.pitch * 180 / Math.PI).toFixed(1)}°`;
    document.getElementById(`euler-yaw-${deviceId}`).textContent = `${(euler.yaw * 180 / Math.PI).toFixed(1)}°`;
    
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
    
    updateBar(`quat-bar-x-${deviceId}`, x);
    updateBar(`quat-bar-y-${deviceId}`, y);
    updateBar(`quat-bar-z-${deviceId}`, z);
    updateBar(`quat-bar-w-${deviceId}`, w);
}
