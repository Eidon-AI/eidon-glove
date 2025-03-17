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
    { finger: 1, joint: 0, type: 'MCP_ABDUCTION', min: 0, max: 255, inverted: false },
    { finger: 1, joint: 1, type: 'MCP_FLEXION', min: 0, max: 255, inverted: false },
    { finger: 1, joint: 2, type: 'PIP_FLEXION', min: 0, max: 158, inverted: false },
    
    // Middle finger (3 joints)
    { finger: 2, joint: 0, type: 'MCP_ABDUCTION', min: 120, max: 250, inverted: false },
    { finger: 2, joint: 1, type: 'MCP_FLEXION', min: 20, max: 246, inverted: false },
    { finger: 2, joint: 2, type: 'PIP_FLEXION', min: 8, max: 140, inverted: false },
    
    // Ring finger (3 joints)
    { finger: 3, joint: 0, type: 'MCP_ABDUCTION', min: 0, max: 255, inverted: false },
    { finger: 3, joint: 1, type: 'MCP_FLEXION', min: 0, max: 255, inverted: false },
    { finger: 3, joint: 2, type: 'PIP_FLEXION', min: 0, max: 255, inverted: false },
    
    // Pinky finger (3 joints)
    { finger: 4, joint: 0, type: 'MCP_ABDUCTION', min: 0, max: 255, inverted: false },
    { finger: 4, joint: 1, type: 'MCP_FLEXION', min: 0, max: 255, inverted: false },
    { finger: 4, joint: 2, type: 'PIP_FLEXION', min: 35, max: 100, inverted: false }
];

// Add gamepad support
let gamepadIndex = null;
let gamepadConnected = false;

// Initialize gamepad connection listeners
window.addEventListener("gamepadconnected", (e) => {
    console.log("Gamepad connected:", e.gamepad.id);
    gamepadIndex = e.gamepad.index;
    gamepadConnected = true;
    
    // Log the number of axes detected
    console.log(`Detected ${e.gamepad.axes.length} axes`);
    
    // Update status indicator if it exists
    if (statusIndicator) {
        statusIndicator.textContent = `Status: Connected to gamepad: ${e.gamepad.id}`;
        statusIndicator.className = 'status-connected';
    }
    
    // Start the gamepad polling loop
    requestAnimationFrame(pollGamepad);
    
    // Add log message if the function exists
    if (typeof addLogMessage === 'function') {
        addLogMessage(`Connected to gamepad: ${e.gamepad.id} with ${e.gamepad.axes.length} axes`);
        addLogMessage(`Note: Gamepad input will respect joint inversion settings`);
    } else {
        console.log(`Connected to gamepad: ${e.gamepad.id} with ${e.gamepad.axes.length} axes`);
        console.log(`Note: Gamepad input will respect joint inversion settings`);
    }
});

window.addEventListener("gamepaddisconnected", (e) => {
    console.log("Gamepad disconnected:", e.gamepad.id);
    if (gamepadIndex === e.gamepad.index) {
        gamepadIndex = null;
        gamepadConnected = false;
        
        // Update status indicator if it exists
        if (statusIndicator) {
            statusIndicator.textContent = 'Status: Gamepad disconnected';
            statusIndicator.className = '';
        }
        
        // Add log message if the function exists
        if (typeof addLogMessage === 'function') {
            addLogMessage(`Disconnected from gamepad: ${e.gamepad.id}`);
        } else {
            console.log(`Disconnected from gamepad: ${e.gamepad.id}`);
        }
    }
});

// Function to poll gamepad state
function pollGamepad() {
    if (gamepadConnected && gamepadIndex !== null) {
        const gamepad = navigator.getGamepads()[gamepadIndex];
        
        if (gamepad && !ignoreExternalInput) { // Only process gamepad input if not ignoring external input
            // Process axes values
            const numAxes = Math.min(gamepad.axes.length, MAX_JOINTS);
            
            for (let i = 0; i < numAxes; i++) {
                // Convert from -1 to 1 range to 0 to 255 range for axes that use this range
                let value;
                
                // Some axes might be in 0 to 1 range (like triggers)
                // Check if this is a trigger-like axis (usually axes 4 and 5)
                if (i === 4 || i === 5) {
                    // Triggers often use 0 to 1 range
                    value = Math.round(((gamepad.axes[i] + 1) / 2) * 255);
                } else {
                    // Standard axes use -1 to 1 range
                    value = Math.round((gamepad.axes[i] + 1) * 127.5);
                }
                
                // Apply inversion if needed, just like with serial input
                if (fingerJointMap[i]?.inverted) {
                    // Invert the value based on the type of joint
                    if (fingerJointMap[i].type.includes('ABDUCTION')) {
                        // For abduction (where 127 is center), invert around 127
                        value = 255 - value;
                    } else {
                        // For flexion, simply invert the range
                        const min = fingerJointMap[i].min;
                        const max = fingerJointMap[i].max;
                        value = max - (value - min);
                    }
                }
                
                // Update joint value
                jointValues[i] = value;
                
                // Update the joint display
                updateJointDisplay(i, value);
            }
            
            // If we have fewer axes than joints, try to use buttons for the remaining joints
            if (numAxes < MAX_JOINTS) {
                for (let i = numAxes; i < MAX_JOINTS && (i - numAxes) < gamepad.buttons.length; i++) {
                    // Buttons use 0 to 1 range
                    let value = Math.round(gamepad.buttons[i - numAxes].value * 255);
                    
                    // Apply inversion if needed, just like with serial input
                    if (fingerJointMap[i]?.inverted) {
                        // Invert the value based on the type of joint
                        if (fingerJointMap[i].type.includes('ABDUCTION')) {
                            // For abduction (where 127 is center), invert around 127
                            value = 255 - value;
                        } else {
                            // For flexion, simply invert the range
                            const min = fingerJointMap[i].min;
                            const max = fingerJointMap[i].max;
                            value = max - (value - min);
                        }
                    }
                    
                    // Update joint value
                    jointValues[i] = value;
                    
                    // Update the joint display
                    updateJointDisplay(i, value);
                }
            }
            
            // Update the hand model if the function exists
            if (typeof updateHandModel === 'function') {
                updateHandModel();
            }
        }
        
        // Continue polling
        requestAnimationFrame(pollGamepad);
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

// Add log message
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

// Process incoming data
function processData(data) {
    inputBuffer += data;
    
    // Process complete lines
    let lineEndIndex;
    while ((lineEndIndex = inputBuffer.indexOf('\n')) !== -1) {
        const line = inputBuffer.substring(0, lineEndIndex).trim();
        inputBuffer = inputBuffer.substring(lineEndIndex + 1);
        
        // Check if line matches the expected format
        const jointMatch = line.match(/^>Joint_(\d+):(\d+)$/);
        if (jointMatch && !ignoreExternalInput) { // Only process joint data if not ignoring external input
            const jointIndex = parseInt(jointMatch[1], 10);
            let jointValue = parseInt(jointMatch[2], 10);
            
            if (jointIndex >= 0 && jointIndex < MAX_JOINTS) {
                // Check if this joint's values should be inverted
                if (fingerJointMap[jointIndex]?.inverted) {
                    // Invert the value based on the type of joint
                    if (fingerJointMap[jointIndex].type.includes('ABDUCTION')) {
                        // For abduction (where 127 is center), invert around 127
                        jointValue = 255 - jointValue;
                    } else {
                        // For flexion, simply invert the range
                        const min = fingerJointMap[jointIndex].min;
                        const max = fingerJointMap[jointIndex].max;
                        jointValue = max - (jointValue - min);
                    }
                }
                
                jointValues[jointIndex] = jointValue;
                updateJointDisplay(jointIndex, jointValue);
                updateHandModel();
            }
        } else {
            // Log other messages
            addLogMessage(`Received: ${line}`);
        }
    }
}

// Read from the serial port
async function readSerialData() {
    while (port.readable && keepReading) {
        reader = port.readable.getReader();
        
        try {
            while (true) {
                const { value, done } = await reader.read();
                
                if (done) {
                    break;
                }
                
                if (value) {
                    processData(decoder.decode(value));
                }
            }
        } catch (error) {
            console.error('Error reading data:', error);
            addLogMessage(`Error: ${error.message}`);
        } finally {
            reader.releaseLock();
        }
    }
}

// Connect to the device
async function connectToDevice() {
    try {
        // Request a port
        port = await navigator.serial.requestPort();
        
        // Get the selected baud rate
        const baudRate = parseInt(baudRateSelect.value, 10);
        
        // Add a delay before opening the port
        addLogMessage("Port selected, preparing to connect...");
        await new Promise(resolve => setTimeout(resolve, 500));
        
        let connected = false;
        
        // First attempt - standard connection
        try {
            addLogMessage("Attempting to open port...");
            await port.open({ 
                baudRate: baudRate,
                dataBits: 8,
                stopBits: 1,
                parity: "none",
                bufferSize: 4096,
                flowControl: "none"
            });
            connected = true;
            addLogMessage("Port opened successfully.");
        } catch (openError) {
            addLogMessage(`First connection attempt failed: ${openError.message}`);
            
            // Second attempt - with a delay
            try {
                addLogMessage("Trying again with delay...");
                await new Promise(resolve => setTimeout(resolve, 1000));
                await port.open({ baudRate: baudRate });
                connected = true;
                addLogMessage("Port opened successfully on second attempt.");
            } catch (retryError) {
                addLogMessage(`Second attempt failed: ${retryError.message}`);
                throw new Error(`Could not open serial port: ${retryError.message}`);
            }
        }
        
        if (connected) {
            // Update UI
            statusIndicator.textContent = 'Status: Connected';
            statusIndicator.className = 'status connected';
            connectButton.disabled = true;
            disconnectButton.disabled = false;
            baudRateSelect.disabled = true;
            
            // Initialize joint elements
            initializeJointElements();
            
            // Log connection
            addLogMessage(`Connected at ${baudRate} baud`);
            
            // Start reading
            keepReading = true;
            readSerialData();
        }
        
    } catch (error) {
        console.error('Error connecting to device:', error);
        addLogMessage(`Connection error: ${error.message}`);
        
        // More detailed error handling
        if (error.message.includes("serial port")) {
            addLogMessage("Troubleshooting tips:");
            addLogMessage("1. Make sure no other applications are using this port");
            addLogMessage("2. Disconnect and reconnect the device");
            addLogMessage("3. Try a different USB port");
            addLogMessage("4. Restart your browser");
            addLogMessage("5. Check if your device requires specific drivers");
        }
        
        // Reset UI state to allow reconnection attempts
        statusIndicator.textContent = 'Status: Connection failed';
        statusIndicator.className = 'status disconnected';
        connectButton.disabled = false;
        disconnectButton.disabled = true;
        baudRateSelect.disabled = false;
        
        // Reset port variable - don't try to close it if it failed to open
        port = null;
    }
}

// Disconnect from the device
async function disconnectFromDevice() {
    if (port) {
        keepReading = false;
        
        // Close the port
        try {
            await port.close();
            
            // Update UI
            statusIndicator.textContent = 'Status: Disconnected';
            statusIndicator.className = 'status disconnected';
            connectButton.disabled = false;
            disconnectButton.disabled = true;
            baudRateSelect.disabled = false;
            
            // Log disconnection
            addLogMessage('Disconnected');
            
        } catch (error) {
            console.error('Error disconnecting:', error);
            addLogMessage(`Disconnection error: ${error.message}`);
        }
    }
}

// Initialize Three.js scene
function initThreeJS() {
    // Create scene
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0xf0f0f0);
    
    // Create camera
    camera = new THREE.PerspectiveCamera(
        75, // Field of view
        canvasContainer.clientWidth / canvasContainer.clientHeight, // Aspect ratio
        0.1, // Near clipping plane
        1000 // Far clipping plane
    );
    camera.position.set(0, 15, 15);
    camera.lookAt(0, 0, 0);
    
    // Create renderer
    renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(canvasContainer.clientWidth, canvasContainer.clientHeight);
    renderer.setPixelRatio(window.devicePixelRatio);
    canvasContainer.appendChild(renderer.domElement);
    
    // Add orbit controls
    controls = new THREE.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.25;
    
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

// Add GLTFLoader to your imports (you'll need to include this in your HTML)
// <script src="https://cdn.jsdelivr.net/npm/three@0.132.2/examples/js/loaders/GLTFLoader.js"></script>

// In your global variables section, add:
let handModel; // The loaded GLB model
let handSkeleton = {}; // Object to store references to the bones

// Replace the createHandModel function with this:
function createHandModel() {
    // Create a temporary placeholder while the model loads
    const placeholder = new THREE.Group();
    scene.add(placeholder);
    hand.palm = placeholder;
    
    // Initialize empty fingers array
    hand.fingers = [];
    
    // Load the GLB model
    const loader = new THREE.GLTFLoader();
    loader.load(
        'rigged_hand1.glb', // Path to your hand model
        function(gltf) {
            // Success callback
            handModel = gltf.scene;
            
            // Scale and position the model as needed
            handModel.scale.set(5, 5, 5); // Adjust scale as needed
            handModel.position.set(0, 0, 0);
            handModel.rotation.set(0, 0, 0);
            
            // Replace the placeholder with the loaded model
            scene.remove(placeholder);
            scene.add(handModel);
            hand.palm = handModel;
            
            // Store references to the bones for animation
            const bones = findBonesInModel(handModel);
            
            // Map bones to our structure
            mapBonesToFingers(bones);
            
            // Add a simple hand label
            addHandLabel();
            
            // Log success
            addLogMessage("Hand model loaded successfully");
        },
        function(xhr) {
            // Progress callback
            const percent = (xhr.loaded / xhr.total * 100).toFixed(1);
            addLogMessage(`Loading model: ${percent}%`);
        },
        function(error) {
            // Error callback
            console.error('Error loading model:', error);
            addLogMessage(`Error loading hand model: ${error.message}`);
            
            // Fall back to the geometric model if loading fails
            createGeometricHandModel();
        }
    );
}

// Helper function to find bones in the model
function findBonesInModel(model) {
    const bones = {};
    
    model.traverse(function(object) {
        // Check if this is a bone (usually it's an Object3D with a specific naming convention)
        if (object.isBone || (object.type === 'Object3D' && object.name.includes('bone'))) {
            bones[object.name] = object;
        }
    });
    
    return bones;
}

// Map the bones to our finger structure
function mapBonesToFingers(bones) {
    // This mapping will depend on your specific model's bone naming convention
    handSkeleton = {
        thumb: {
            cmc: bones['thumb_cmc'] || bones['thumb.001'],
            mcp: bones['thumb_mcp'] || bones['thumb.002'],
            ip: bones['thumb_ip'] || bones['thumb.003']
        },
        index: {
            mcp: bones['index_mcp'] || bones['index.001'],
            pip: bones['index_pip'] || bones['index.002'],
            dip: bones['index_dip'] || bones['index.003']
        },
        middle: {
            mcp: bones['middle_mcp'] || bones['middle.001'],
            pip: bones['middle_pip'] || bones['middle.002'],
            dip: bones['middle_dip'] || bones['middle.003']
        },
        ring: {
            mcp: bones['ring_mcp'] || bones['ring.001'],
            pip: bones['ring_pip'] || bones['ring.002'],
            dip: bones['ring_dip'] || bones['ring.003']
        },
        pinky: {
            mcp: bones['pinky_mcp'] || bones['pinky.001'],
            pip: bones['pinky_pip'] || bones['pinky.002'],
            dip: bones['pinky_dip'] || bones['pinky.003']
        }
    };
    
    // Create a simplified structure that works with our existing code
    hand.fingers = [
        { 
            name: 'Thumb', 
            segments: [handSkeleton.thumb.cmc, handSkeleton.thumb.mcp, handSkeleton.thumb.ip],
            isRigged: true 
        },
        { 
            name: 'Index', 
            segments: [handSkeleton.index.mcp, handSkeleton.index.pip, handSkeleton.index.dip],
            isRigged: true 
        },
        { 
            name: 'Middle', 
            segments: [handSkeleton.middle.mcp, handSkeleton.middle.pip, handSkeleton.middle.dip],
            isRigged: true 
        },
        { 
            name: 'Ring', 
            segments: [handSkeleton.ring.mcp, handSkeleton.ring.pip, handSkeleton.ring.dip],
            isRigged: true 
        },
        { 
            name: 'Pinky', 
            segments: [handSkeleton.pinky.mcp, handSkeleton.pinky.pip, handSkeleton.pinky.dip],
            isRigged: true 
        }
    ];
}

// Keep the original geometric model creation as a fallback
function createGeometricHandModel() {
    // Your existing createHandModel code here
    // Rename it to createGeometricHandModel
    
    // Create materials
    const palmMaterial = new THREE.MeshPhongMaterial({ color: 0xf5c396 });
    const fingerMaterial = new THREE.MeshPhongMaterial({ color: 0xf5c396 });
    const jointMaterial = new THREE.MeshPhongMaterial({ color: 0xe3a977 });
    
    // Create palm
    const palmGeometry = new THREE.BoxGeometry(7, 1, 8);
    hand.palm = new THREE.Mesh(palmGeometry, palmMaterial);
    hand.palm.position.set(0, 0, 0);
    
    // Rotate the entire hand to show palm facing up
    hand.palm.rotation.x = Math.PI; // Rotate 180 degrees around X axis
    scene.add(hand.palm);
    
    // Finger dimensions
    const fingerWidth = 1;
    const fingerHeight = 0.8;
    const fingerSegmentLengths = [3, 2, 1.5]; // MCP, PIP, DIP segment lengths
    const thumbSegmentLengths = [2, 2, 1.5]; // CMC, MCP, IP segment lengths
    
    // Finger positions relative to palm for RIGHT hand with palm facing up
    const fingerBasePositions = [
        [4, 1.5, -2],         // Thumb (moved further outward and forward)
        [1.5, -0.5, -4],      // Index (right)
        [0, -0.5, -4],        // Middle (center)
        [-1.5, -0.5, -4],     // Ring (left)
        [-3, -0.5, -4]        // Pinky (far left)
    ];
    
    // Initial finger rotations (for natural hand pose)
    const fingerBaseRotations = [
        { x: 0, y: -Math.PI / 6, z: Math.PI / 2.5 },  // Thumb (adjusted for better outward position)
        { x: 0, y: -Math.PI / 48, z: 0 },            // Index (slight inward tilt)
        { x: 0, y: 0, z: 0 },                        // Middle (no tilt - perfectly straight)
        { x: 0, y: Math.PI / 32, z: 0 },             // Ring (slight outward tilt)
        { x: 0, y: Math.PI / 24, z: 0 }              // Pinky (more outward tilt)
    ];
    
    // Create fingers
    for (let f = 0; f < 5; f++) {
        const finger = {
            name: ['Thumb', 'Index', 'Middle', 'Ring', 'Pinky'][f],
            segments: [],
            joints: [],
            isRigged: false // Mark as not rigged
        };
        
        // Determine if this is the thumb
        const isThumb = f === 0;
        const segmentLengths = isThumb ? thumbSegmentLengths : fingerSegmentLengths;
        const segmentCount = segmentLengths.length;
        
        // Create finger base group (attached to palm)
        finger.group = new THREE.Group();
        finger.group.position.set(...fingerBasePositions[f]);
        
        // Apply initial rotations for natural pose
        const baseRotation = fingerBaseRotations[f];
        finger.group.rotation.x = baseRotation.x;
        finger.group.rotation.y = baseRotation.y;
        finger.group.rotation.z = baseRotation.z;
        
        hand.palm.add(finger.group);
        
        // Create segments and joints
        let parentGroup = finger.group;
        
        for (let s = 0; s < segmentCount; s++) {
            // Create segment group
            const segmentGroup = new THREE.Group();
            
            // Create joint sphere at the base of the segment
            const jointGeometry = new THREE.SphereGeometry(fingerWidth * 0.6, 8, 8);
            const joint = new THREE.Mesh(jointGeometry, jointMaterial);
            segmentGroup.add(joint);
            
            // Create segment box, positioned so its base is at the joint
            const segmentGeometry = new THREE.BoxGeometry(fingerWidth, fingerHeight, segmentLengths[s]);
            const segment = new THREE.Mesh(segmentGeometry, fingerMaterial);
            segment.position.z = -segmentLengths[s] / 2; // Position relative to joint
            segmentGroup.add(segment);
            
            // Add segment group to parent
            parentGroup.add(segmentGroup);
            
            // Store references
            finger.segments.push(segmentGroup);
            finger.joints.push(joint);
            
            // Position next segment at the end of this one
            if (s < segmentCount - 1) {
                // Create a connector group that will be positioned at the end of this segment
                const connectorGroup = new THREE.Group();
                connectorGroup.position.z = -segmentLengths[s]; // Position at end of current segment
                segmentGroup.add(connectorGroup);
                
                // The next segment's parent will be this connector
                parentGroup = connectorGroup;
            }
        }
        
        hand.fingers.push(finger);
    }
    
    // Add finger labels for clarity
    addFingerLabels();
    
    // Add a simple hand label using a canvas texture
    addHandLabel();
}

// Function to add finger labels
function addFingerLabels() {
    // Only add labels for the geometric model
    if (!handModel) {
        const fingerNames = ['Thumb', 'Index', 'Middle', 'Ring', 'Pinky'];
        
        for (let i = 0; i < hand.fingers.length; i++) {
            const finger = hand.fingers[i];
            
            // Skip if this finger doesn't have a group (rigged model)
            if (!finger.group) continue;
            
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
            context.fillText(fingerNames[i], canvas.width / 2, canvas.height / 2);
            
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
            label.position.set(0, -1.5, -2);
            label.rotation.x = Math.PI / 2; // Make it face up
            
            finger.group.add(label);
        }
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

// Update hand model based on joint values
function updateHandModel() {
    // First pass: collect all joint values
    const fingerJointValues = {};
    
    // Initialize the structure to store joint values
    for (let f = 0; f < 5; f++) {
        fingerJointValues[f] = {
            mcpFlexion: null,
            mcpAbduction: null,
            pipFlexion: null,
            dipFlexion: null,
            cmcFlexion: null,
            cmcAbduction: null
        };
    }
    
    // Collect all joint values from the fingerJointMap
    for (let i = 0; i < MAX_JOINTS; i++) {
        const jointInfo = fingerJointMap[i];
        if (!jointInfo) continue;
        
        const { finger, joint, type, min, max } = jointInfo;
        const value = jointValues[i];
        
        // Skip if finger doesn't exist
        if (!hand.fingers[finger]) continue;
        
        // Normalize the value based on joint type
        if (type.includes('FLEXION')) {
            // Calculate normalized value (0-1)
            const normalizedValue = (value - min) / (max - min);
            // Convert to angle (0 to 90 degrees)
            const angle = normalizedValue * Math.PI / 2;
            
            // Store in the appropriate slot
            if (type.includes('MCP_FLEXION')) {
                fingerJointValues[finger].mcpFlexion = angle;
            } else if (type.includes('PIP_FLEXION')) {
                fingerJointValues[finger].pipFlexion = angle;
            } else if (type.includes('IP_FLEXION')) {
                fingerJointValues[finger].dipFlexion = angle;
            } else if (type.includes('CMC_FLEXION')) {
                fingerJointValues[finger].cmcFlexion = angle;
            }
        } 
        else if (type.includes('ABDUCTION')) {
            // For abduction, 127 is center (0 degrees)
            // 0 is full left (-45 degrees) and 255 is full right (45 degrees)
            const normalizedAbduction = (value - 127) / 127; // -1 to 1 range
            
            // Apply different abduction limits and offsets based on finger
            let maxAbductionAngle;
            let abductionOffset = 0; // Default is centered
            
            if (finger === 0) { // Thumb
                // Reduce the maximum abduction angle to prevent rotation into the hand
                maxAbductionAngle = Math.PI / 8; // 22.5 degrees - reduced from 30 degrees
                
                // Add a stronger offset to keep the thumb positioned away from the palm
                abductionOffset = Math.PI / 6; // Increased offset to keep thumb outward
            } else if (finger === 1) { // Index
                maxAbductionAngle = Math.PI / 8; // 22.5 degrees
                abductionOffset = -Math.PI / 32; // Slight bias toward middle finger
            } else if (finger === 2) { // Middle
                maxAbductionAngle = Math.PI / 24; // 22.5 degrees
                abductionOffset = -Math.PI / 24; // Slight bias toward middle finger
            } else if (finger === 3) { // Ring
                maxAbductionAngle = Math.PI / 16; // 11.25 degrees
                abductionOffset = Math.PI / 32; // Slight bias toward pinky
            } else { // Pinky
                maxAbductionAngle = Math.PI / 10; // 18 degrees
                abductionOffset = Math.PI / 24; // Bias toward ring finger
            }
            
            // Calculate angle with offset and limits
            const angle = normalizedAbduction * maxAbductionAngle + abductionOffset;
            
            // Store in the appropriate slot
            if (type.includes('MCP_ABDUCTION')) {
                fingerJointValues[finger].mcpAbduction = angle;
            } else if (type.includes('CMC_ABDUCTION')) {
                fingerJointValues[finger].cmcAbduction = angle;
            }
        }
    }
    
    // Infer DIP flexion from PIP flexion for all fingers except thumb
    // In a real hand, DIP joint typically flexes about 2/3 as much as the PIP joint
    for (let f = 1; f < 5; f++) {
        if (fingerJointValues[f].pipFlexion !== null && fingerJointValues[f].dipFlexion === null) {
            // DIP flexion is typically about 2/3 of PIP flexion
            // This ratio can be adjusted for more realistic movement
            const dipRatio = 0.7; // 70% of PIP flexion
            fingerJointValues[f].dipFlexion = fingerJointValues[f].pipFlexion * dipRatio;
        }
    }
    
    // Second pass: apply all collected values to the hand model
    for (let f = 0; f < 5; f++) {
        // Skip if this finger doesn't exist
        if (!hand.fingers[f]) continue;
        
        const finger = hand.fingers[f];
        const values = fingerJointValues[f];
        
        // Check if we're using the rigged model
        if (finger.isRigged) {
            if (f === 0) { // Thumb
                // Apply rotations to thumb bones
                if (handSkeleton.thumb.cmc && values.cmcAbduction !== null) {
                    // Apply CMC abduction
                    const limitedAbduction = Math.min(values.cmcAbduction, Math.PI / 6);
                    
                    // Apply rotations to the bone
                    handSkeleton.thumb.cmc.rotation.z = Math.PI / 2.5 - limitedAbduction;
                    handSkeleton.thumb.cmc.rotation.x = (limitedAbduction / (Math.PI / 6)) * Math.PI / 24;
                    handSkeleton.thumb.cmc.rotation.y = -Math.PI / 3 - ((limitedAbduction / (Math.PI / 6)) * Math.PI / 24);
                }
                
                if (handSkeleton.thumb.cmc && values.cmcFlexion !== null) {
                    // Apply CMC flexion
                    handSkeleton.thumb.cmc.rotation.x = values.cmcFlexion * 0.8;
                    handSkeleton.thumb.cmc.rotation.y += values.cmcFlexion * 0.3;
                    handSkeleton.thumb.cmc.rotation.z += values.cmcFlexion * 0.1;
                }
                
                if (handSkeleton.thumb.mcp && values.mcpFlexion !== null) {
                    // Apply MCP flexion
                    handSkeleton.thumb.mcp.rotation.x = values.mcpFlexion;
                    handSkeleton.thumb.mcp.rotation.y = values.mcpFlexion * 0.3;
                }
                
                if (handSkeleton.thumb.ip && values.dipFlexion !== null) {
                    // Apply IP flexion
                    handSkeleton.thumb.ip.rotation.x = values.dipFlexion;
                    handSkeleton.thumb.ip.rotation.y = values.dipFlexion * 0.2;
                }
            } else {
                // Other fingers
                const fingerName = ['thumb', 'index', 'middle', 'ring', 'pinky'][f];
                const fingerBones = handSkeleton[fingerName];
                
                if (fingerBones.mcp) {
                    if (values.mcpFlexion !== null) {
                        fingerBones.mcp.rotation.x = values.mcpFlexion;
                    }
                    if (values.mcpAbduction !== null) {
                        fingerBones.mcp.rotation.y = values.mcpAbduction;
                    }
                }
                
                if (fingerBones.pip && values.pipFlexion !== null) {
                    fingerBones.pip.rotation.x = values.pipFlexion;
                }
                
                if (fingerBones.dip && values.dipFlexion !== null) {
                    fingerBones.dip.rotation.x = values.dipFlexion;
                }
            }
        } else {
            // Original geometric model logic
            if (f === 0) { // Thumb has a different structure
                // CMC joint (base of thumb)
                if (values.cmcAbduction !== null && finger.group) {
                    // For thumb CMC abduction, we need to ensure it stays away from the hand
                    // even at maximum abduction
                    
                    // Limit the abduction angle to prevent rotation into the hand
                    const limitedAbduction = Math.min(values.cmcAbduction, Math.PI / 6);
                    
                    // Apply a modified rotation that keeps the thumb in a more natural position
                    finger.group.rotation.z = Math.PI / 2.5 - limitedAbduction;
                    
                    // Calculate opposition factor but limit how much it affects inward movement
                    const oppositionFactor = limitedAbduction / (Math.PI / 6); // Normalize to 0-1 range
                    
                    // Apply minimal X rotation to prevent the thumb from rotating inward
                    finger.group.rotation.x = oppositionFactor * Math.PI / 24;
                    
                    // Adjust Y rotation to keep thumb more outward throughout its range
                    finger.group.rotation.y = -Math.PI / 3 - (oppositionFactor * Math.PI / 24);
                }
                
                // Modify the CMC flexion to prevent the thumb from going into the hand
                if (values.cmcFlexion !== null && finger.segments && finger.segments[0]) {
                    // For thumb CMC flexion, rotate to move toward palm but with limits
                    finger.segments[0].rotation.x = values.cmcFlexion * 0.8; // Reduce flexion range
                    
                    // Adjust Y rotation to move thumb away from palm during flexion
                    finger.segments[0].rotation.y = values.cmcFlexion * 0.3;
                    
                    // Add slight Z rotation to keep thumb away from palm during flexion
                    finger.segments[0].rotation.z = values.cmcFlexion * 0.1;
                }
                
                // MCP joint (middle joint of thumb)
                if (values.mcpFlexion !== null && finger.segments && finger.segments[1]) {
                    // For thumb MCP flexion, rotate to curl inward
                    finger.segments[1].rotation.x = values.mcpFlexion;
                    // Add a slight rotation around Y to move toward other fingers
                    finger.segments[1].rotation.y = values.mcpFlexion * 0.3;
                }
                
                // IP joint (tip joint of thumb)
                if (values.dipFlexion !== null && finger.segments && finger.segments[2]) {
                    // For thumb IP flexion, rotate to curl inward
                    finger.segments[2].rotation.x = values.dipFlexion;
                    // Add a slight rotation around Y to move toward other fingers
                    finger.segments[2].rotation.y = values.dipFlexion * 0.2;
                }
            } else { // Index, middle, ring, pinky
                // MCP joint (base of finger) - handles both flexion and abduction
                if (values.mcpFlexion !== null && finger.segments && finger.segments[0]) {
                    finger.segments[0].rotation.x = values.mcpFlexion;
                }
                if (values.mcpAbduction !== null && finger.segments && finger.segments[0]) {
                    // Special case for middle finger to ensure it's straight at neutral position
                    if (f === 2) { // Middle finger
                        // Apply a small correction to ensure it's perfectly straight at neutral
                        finger.segments[0].rotation.y = values.mcpAbduction;
                    } else {
                        finger.segments[0].rotation.y = values.mcpAbduction;
                    }
                }
                
                // PIP joint (middle joint)
                if (values.pipFlexion !== null && finger.segments && finger.segments[1]) {
                    finger.segments[1].rotation.x = values.pipFlexion;
                }
                
                // DIP joint (tip joint)
                if (values.dipFlexion !== null && finger.segments && finger.segments[2]) {
                    finger.segments[2].rotation.x = values.dipFlexion;
                }
            }
        }
    }
}

// Handle window resize
function onWindowResize() {
    camera.aspect = canvasContainer.clientWidth / canvasContainer.clientHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(canvasContainer.clientWidth, canvasContainer.clientHeight);
}

// Animation loop
function animate() {
    requestAnimationFrame(animate);
    controls.update();
    renderer.render(scene, camera);
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

// Check if Web Serial API is supported
if (!navigator.serial) {
    statusIndicator.textContent = 'Status: Web Serial API not supported in this browser';
    connectButton.disabled = true;
    addLogMessage('ERROR: Web Serial API is not supported in this browser. Try Chrome or Edge.');
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
    const diagnosticButton = document.createElement('button');
    diagnosticButton.textContent = 'Gamepad Info';
    diagnosticButton.className = 'control-button';
    diagnosticButton.onclick = showGamepadInfo;
    
    // Find a suitable parent element
    const controlPanel = document.querySelector('.view-controls');
    if (controlPanel) {
        controlPanel.appendChild(diagnosticButton);
    } else {
        // If no control panel exists, append to body
        document.body.appendChild(diagnosticButton);
    }
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
        addGamepadDiagnosticButton();
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

// Call this at initialization
addRecordingControls();
