// Global variables for serial communication
let port;
let reader;
let keepReading = true;
let decoder = new TextDecoder();
let inputBuffer = '';
const MAX_JOINTS = 16;

// IMU Coordinate System Configuration
// Adjust these values to fix yaw/roll/pitch mapping issues
const IMU_COORDINATE_CONFIG = {
    // Quaternion component mapping (try different combinations)
    // Original: x, y, z, w
    // Option 1: x, z, y, w (swap Y and Z)
    // Option 2: x, -z, y, w (swap Y and Z, negate Z) 
    // Option 3: -x, y, z, w (negate X)
    mapping: 'original', // Start with original since hand is now properly oriented
    
    // Additional rotation adjustments (in radians)
    additionalRotation: {
        x: 0,  // 0° around X-axis (removed since hand model is now oriented correctly)
        y: 0,  // 0° around Y-axis  
        z: 0   // 0° around Z-axis
    }
};

const EIDON_VENDOR_ID   = 0xE1D0;
const EIDON_GLOVE_PID   = 0x0001;
const EIDON_TRACKER_PID = 0x0002;

const toHex = (n) => {
    const num = Number(n); // coerce strings like "0x1d50" or numbers
    if (Number.isNaN(num)) return '0000';
    return num.toString(16).padStart(4, '0');
};

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
const backViewBtn = document.getElementById('back-view-btn');
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
const TRACKER_REPORT_SIZE = 9;
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
let handModelsVisible = true; // Track hand model visibility state

// Add to global variables section
const armJointMap = [
    { joint: 'shoulder', rotationOrder: 'XYZ', min: -180, max: 180 },
    { joint: 'elbow', rotationOrder: 'XYZ', min: 0, max: 145 },
    { joint: 'wrist', rotationOrder: 'XYZ', min: -90, max: 90 }
];

// Add to global variables section
const trackerArrows = new Map(); // Map to store tracker arrows by deviceId

// Add this function to get the current hand count
function getHandCount() {
    return hands.size;
}

// Add function to toggle hand model visibility
function toggleHandModelsVisibility() {
    handModelsVisible = !handModelsVisible;
    const toggleButton = document.getElementById('hand-models-toggle');
    toggleButton.textContent = handModelsVisible ? 'Hide Hand Models' : 'Show Hand Models';
    
    // Update visibility for all hand models (simplified for hand-only display)
    for (const [deviceId, handModel] of hands) {
        if (handModel.handGroup) {
            handModel.handGroup.visible = handModelsVisible;
        }
    }
    
    // Save preference
    localStorage.setItem('handModelsVisible', handModelsVisible);
}

// Add function to initialize hand model visibility
function initializeHandModelVisibility() {
    const toggleButton = document.getElementById('hand-models-toggle');
    if (toggleButton) {
        toggleButton.onclick = toggleHandModelsVisibility;
        
        // Check for saved preference
        const savedVisibility = localStorage.getItem('handModelsVisible');
        if (savedVisibility !== null) {
            handModelsVisible = savedVisibility === 'true';
            toggleButton.textContent = handModelsVisible ? 'Hide Hand Models' : 'Show Hand Models';
            
            // Apply saved visibility state (simplified for hand-only display)
            for (const [deviceId, handModel] of hands) {
                if (handModel.handGroup) {
                    handModel.handGroup.visible = handModelsVisible;
                }
            }
        }
    }
}

// Initialize Three.js scene
function initThreeJS() {
    // Check if canvasContainer exists
    if (!canvasContainer) {
        console.error('Canvas container not found');
        return;
    }

    // Create scene
    scene = new THREE.Scene();
    const isDark = document.body.getAttribute('data-theme') === 'dark';
    scene.background = new THREE.Color(isDark ? 0x1a1a1a : 0xf0f0f0);
    
    // Create camera
    camera = new THREE.PerspectiveCamera(
        75,
        canvasContainer.clientWidth / canvasContainer.clientHeight,
        0.1,
        1000
    );
    camera.position.set(0, 8, 15);  // Adjusted to better view centered hand at y=8
    camera.lookAt(0, 8, 0);  // Look at hand position instead of origin
    
    // Create renderer
    renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(canvasContainer.clientWidth, canvasContainer.clientHeight);
    const DPR = Math.min(1.5, window.devicePixelRatio);  // cap at 1.5
    renderer.setPixelRatio(DPR);
    
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
    controls.target.set(0, 8, 0);  // Set orbit target to hand position
    
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

    // Add coordinate axes
    const axesLength = 10;
    const axesColors = [0xff0000, 0x00ff00, 0x0000ff]; // Red, Green, Blue
    
    // X axis (Red)
    const xAxisGeometry = new THREE.BufferGeometry();
    const xAxisMaterial = new THREE.LineBasicMaterial({ color: axesColors[0] });
    const xAxis = new THREE.Line(
        xAxisGeometry,
        xAxisMaterial
    );
    xAxisGeometry.setFromPoints([
        new THREE.Vector3(0, 0, 0),
        new THREE.Vector3(axesLength, 0, 0)
    ]);
    scene.add(xAxis);

    // Y axis (Green)
    const yAxisGeometry = new THREE.BufferGeometry();
    const yAxisMaterial = new THREE.LineBasicMaterial({ color: axesColors[1] });
    const yAxis = new THREE.Line(
        yAxisGeometry,
        yAxisMaterial
    );
    yAxisGeometry.setFromPoints([
        new THREE.Vector3(0, 0, 0),
        new THREE.Vector3(0, axesLength, 0)
    ]);
    scene.add(yAxis);

    // Z axis (Blue)
    const zAxisGeometry = new THREE.BufferGeometry();
    const zAxisMaterial = new THREE.LineBasicMaterial({ color: axesColors[2] });
    const zAxis = new THREE.Line(
        zAxisGeometry,
        zAxisMaterial
    );
    zAxisGeometry.setFromPoints([
        new THREE.Vector3(0, 0, 0),
        new THREE.Vector3(0, 0, axesLength)
    ]);
    scene.add(zAxis);

    // Add axis labels
    const labelSize = 0.5;
    const labelDistance = axesLength + 0.5;
    
    // X label
    const xLabel = createTextSprite('X', axesColors[0]);
    xLabel.position.set(labelDistance, 0, 0);
    scene.add(xLabel);

    // Y label
    const yLabel = createTextSprite('Y', axesColors[1]);
    yLabel.position.set(0, labelDistance, 0);
    scene.add(yLabel);

    // Z label
    const zLabel = createTextSprite('Z', axesColors[2]);
    zLabel.position.set(0, 0, labelDistance);
    scene.add(zLabel);
    
    // Handle window resize
    window.addEventListener('resize', onWindowResize);
    
    // Start animation loop
    animate();
}

// Add helper function to create text sprites
function createTextSprite(text, color) {
    const canvas = document.createElement('canvas');
    const context = canvas.getContext('2d');
    canvas.width = 64;
    canvas.height = 64;
    
    // Draw text
    context.font = 'Bold 32px Arial';
    context.fillStyle = `#${color.toString(16).padStart(6, '0')}`;
    context.textAlign = 'center';
    context.textBaseline = 'middle';
    context.fillText(text, canvas.width/2, canvas.height/2);
    
    // Create texture
    const texture = new THREE.CanvasTexture(canvas);
    const material = new THREE.SpriteMaterial({ map: texture });
    const sprite = new THREE.Sprite(material);
    sprite.scale.set(1, 1, 1);
    
    return sprite;
}

// Modify createHandModel to create a hand for a specific device
function createHandModel(deviceId) {
    if (hands.has(deviceId)) {
        return hands.get(deviceId);
    }

    const handCount = getHandCount();
    const gloveInfo = gloves.get(deviceId);
    const isRightHand = gloveInfo ? gloveInfo.isRightHand : false;
    const handModel = {
        // Comment out arm components for simplified hand-only display
        // arm: {
        //     shoulder: null,
        //     upperArm: null,
        //     elbow: null,
        //     forearm: null,
        //     wrist: null
        // },
        handGroup: null,  // Main hand group for IMU orientation
        palm: null,
        fingers: [],
        // Track which side this hand represents for easier mapping
        side: isRightHand ? 'right' : 'left',
        id: deviceId
    };

    // Create materials
    const palmMaterial = new THREE.MeshPhongMaterial({ color: 0xf5c396 });
    const fingerMaterial = new THREE.MeshPhongMaterial({ color: 0xf5c396 });
    const jointMaterial = new THREE.MeshPhongMaterial({ color: 0xe3a977 });

    // Create main hand group positioned at center, elevated above ground
    handModel.handGroup = new THREE.Group();
    handModel.handGroup.position.set(0, 8, 0); // Centered horizontally, 8 units above ground
    
    scene.add(handModel.handGroup);

    // Comment out arm creation - simplified to hand only
    /*
    // Create arm components
    // Shoulder joint (sphere)
    const shoulderGeometry = new THREE.SphereGeometry(1.5, 16, 16);
    handModel.arm.shoulder = new THREE.Mesh(shoulderGeometry, jointMaterial);
    handModel.arm.shoulder.position.set(isRightHand ? 4 : -4, 15, 0); // Position shoulder higher up and space them out

    scene.add(handModel.arm.shoulder);

    // Upper arm (cylinder)
    const upperArmGeometry = new THREE.CylinderGeometry(1.2, 1, 8, 16);
    handModel.arm.upperArm = new THREE.Mesh(upperArmGeometry, armMaterial);
    handModel.arm.upperArm.position.set(0, -4, 0); // Position relative to shoulder
    handModel.arm.shoulder.add(handModel.arm.upperArm);

    // Elbow joint (sphere)
    const elbowGeometry = new THREE.SphereGeometry(1.2, 16, 16);
    handModel.arm.elbow = new THREE.Mesh(elbowGeometry, jointMaterial);
    handModel.arm.elbow.position.set(0, -4, 0); // Position at end of upper arm
    handModel.arm.elbow.rotation.y = THREE.MathUtils.degToRad(180);
    handModel.arm.elbow.rotation.x = THREE.MathUtils.degToRad(90);
    handModel.arm.upperArm.add(handModel.arm.elbow);

    // Forearm (cylinder)
    const forearmGeometry = new THREE.CylinderGeometry(1, 0.8, 8, 16);
    handModel.arm.forearm = new THREE.Mesh(forearmGeometry, armMaterial);
    handModel.arm.forearm.position.set(0, -4, 0); // Position relative to elbow
    handModel.arm.elbow.add(handModel.arm.forearm);

    // Wrist joint (sphere)
    const wristGeometry = new THREE.SphereGeometry(1, 16, 16);
    handModel.arm.wrist = new THREE.Mesh(wristGeometry, jointMaterial);
    handModel.arm.wrist.position.set(0, -4, 0); // Position at end of forearm
    handModel.arm.forearm.add(handModel.arm.wrist);

    // Create palm and attach to wrist
    const palmGeometry = new THREE.BoxGeometry(6, 1.25, 7);
    handModel.palm = new THREE.Mesh(palmGeometry, palmMaterial);
    handModel.palm.position.set(0, 0, 4);
    handModel.palm.rotation.x = Math.PI;
    handModel.arm.wrist.add(handModel.palm);
    */

    // Create palm and attach directly to hand group (simplified)
    const palmGeometry = new THREE.BoxGeometry(6, 1.25, 7);
    handModel.palm = new THREE.Mesh(palmGeometry, palmMaterial);
    handModel.palm.position.set(0, 0, 0);
    
    // Apply base rotation to flip hand so palm faces down (180° around X-axis)
    handModel.palm.rotation.x = Math.PI; // 180 degrees to flip palm down
    handModel.palm.rotation.y = Math.PI; // 180 degrees to flip palm down
    
    handModel.handGroup.add(handModel.palm);

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
            // Remove event listener first
            device.removeEventListener('inputreport', handleHIDInput);
            await device.close();
            hidDevices.delete(deviceId);
            
            // Remove from localStorage
            const updatedDevices = savedDevices.filter(id => id !== deviceId);
            localStorage.setItem('hidDevices', JSON.stringify(updatedDevices));
            
            // Clean up UI and 3D elements
            cleanupDevice(deviceId);
            
            // Clear this device's permissions
            try {
                await device.forget();
            } catch (error) {
                console.error(`Error clearing HID permissions for device ${deviceId}:`, error);
            }
            
            addLogMessage(`Disconnected from HID device: ${device.productName}`);
        }
    } else {
        // Disconnect all devices
        for (const [id, device] of hidDevices) {
            // Remove event listener first
            device.removeEventListener('inputreport', handleHIDInput);
            await device.close();
            cleanupDevice(id);
            addLogMessage(`Disconnected from HID device: ${device.productName}`);
        }
        hidDevices.clear();
        localStorage.setItem('hidDevices', '[]');
        
        // Clear all HID device permissions
        try {
            const devices = await navigator.hid.getDevices();
            for (const device of devices) {
                await device.forget();
            }
        } catch (error) {
            console.error('Error clearing HID permissions:', error);
        }
    }
    
    updateConnectionStatus();
    
    // Update device grouping display if in chain mode
    if (chainMode) {
        repositionTrackerArrows();
    }
}

// Add function to clean up device-specific elements
function cleanupDevice(deviceId) {
    // Remove tracker UI and data if it's a tracker
    if (trackers.has(deviceId)) {
        const trackerElement = document.getElementById(`tracker-${deviceId}`);
        if (trackerElement) {
            trackerElement.remove();
        }
        
        // Remove tracker arrow from Three.js scene (handled later generically)
        
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

    // Remove tracker arrow (for trackers or gloves) if it exists
    if (trackerArrows.has(deviceId)) {
        const arrowObj = trackerArrows.get(deviceId);
        const group = arrowObj.group || arrowObj; // support previous structure
        if (group && scene) {
            scene.remove(group);
            // Dispose geometries & materials to free GPU memory
            group.traverse(child => {
                if (child.geometry) child.geometry.dispose();
                if (child.material) {
                    if (Array.isArray(child.material)) child.material.forEach(m => m.dispose());
                    else child.material.dispose();
                }
            });
        }
        trackerArrows.delete(deviceId);
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
    
    // Apply IMU quaternion data to hand orientation (simplified - no arm)
    if (gloveData.quaternion && handModel.handGroup) {
        const { x, y, z, w } = gloveData.quaternion;
        
        // Let's try the original quaternion with a systematic coordinate transformation
        // Device coordinate system might be: X=right, Y=forward, Z=up
        // Hand model needs: X=right, Y=up, Z=forward
        // This suggests we need: device_Y -> hand_Z, device_Z -> hand_Y
        
        // Try mapping: device(x,y,z,w) -> hand(x,z,-y,w)
        // This maps: device_Y->hand_Z (forward), device_Z->hand_Y (up), negate Y for correct direction
        const correctedQuaternion = new THREE.Quaternion(x, z, -y, w);
        
        // Apply the corrected quaternion
        handModel.handGroup.quaternion.copy(correctedQuaternion);
    }
    
    // Comment out arm position update since we're hand-only now
    // updateArmPosition(deviceId);
    
    // Comment out arm values display since we removed arms
    // updateArmValuesDisplay();

    // Process each joint for finger movements
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
backViewBtn.addEventListener('click', () => {
    camera.position.set(0, 8, 20);  // Focus on hand position
    camera.lookAt(0, 8, 0);
    controls.update();
});

sideViewBtn.addEventListener('click', () => {
    camera.position.set(20, 8, 0);  // Focus on hand position
    camera.lookAt(0, 8, 0);
    controls.update();
});

topViewBtn.addEventListener('click', () => {
    camera.position.set(0, 20, 0);  // Look down at hand
    camera.lookAt(0, 8, 0);
    controls.update();
});

resetViewBtn.addEventListener('click', () => {
    camera.position.set(0, 8, 15);  // Reset to initial hand-focused view
    camera.lookAt(0, 8, 0);
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

// NEW: create placeholder left & right hands at startup (no devices required)
// initializeDefaultHands();

// Initialize joint elements
// initializeJointElements();

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
        // console.log('Gamepad API is supported. Connect a gamepad to begin.');
        // if (typeof addLogMessage === 'function') {
        //     addLogMessage('Gamepad API is supported. Connect a gamepad to begin.');
        // }
        
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
    // Create a frame with current timestamp and data for all connected gloves
    const frame = {
        timestamp: Date.now() - recordingStartTime, // Relative time in ms
        gloves: {} // Store data for each glove device
    };
    
    // Record data for each connected glove
    for (const [deviceId, gloveData] of gloves) {
        frame.gloves[deviceId] = {
            jointValues: [...gloveData.jointValues], // Clone the joint values
            quaternion: { ...gloveData.quaternion }, // Clone the quaternion
            euler: { ...gloveData.euler } // Clone the euler angles
        };
    }
    
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
    
    // Check if this is a legacy recording format (has joints array)
    if (currentFrame.joints) {
        // Handle legacy format - apply to first connected glove if any
        const firstGloveId = Array.from(gloves.keys())[0];
        if (firstGloveId) {
            applyLegacyFrame(currentFrame, firstGloveId);
        }
        return;
    }
    
    // Handle new format with device-specific data
    if (currentFrame.gloves) {
        // If there's a next frame, interpolate between frames
        if (currentPlaybackIndex < movement.length - 1) {
            const nextFrame = movement[currentPlaybackIndex + 1];
            const frameDuration = nextFrame.timestamp - currentFrame.timestamp;
            
            if (frameDuration > 0 && nextFrame.gloves) {
                const frameProgress = (elapsedTime - currentFrame.timestamp) / frameDuration;
                
                // Interpolate data for each glove
                for (const [deviceId, currentGloveData] of Object.entries(currentFrame.gloves)) {
                    const nextGloveData = nextFrame.gloves[deviceId];
                    if (nextGloveData && gloves.has(deviceId)) {
                        // Interpolate joint values
                        const gloveData = gloves.get(deviceId);
                        for (let i = 0; i < Math.min(currentGloveData.jointValues.length, gloveData.jointValues.length); i++) {
                            const startValue = currentGloveData.jointValues[i];
                            const endValue = nextGloveData.jointValues[i];
                            gloveData.jointValues[i] = Math.round(startValue + (endValue - startValue) * frameProgress);
                            
                            // Update the joint display
                            updateJointDisplay(deviceId, i, gloveData.jointValues[i]);
                        }
                        
                        // Update quaternion and hand model
                        if (currentGloveData.quaternion && nextGloveData.quaternion) {
                            // For quaternions, we could interpolate but for simplicity, just use current frame
                            gloveData.quaternion = { ...currentGloveData.quaternion };
                            updateQuaternionDisplay(deviceId, 
                                gloveData.quaternion.x, 
                                gloveData.quaternion.y, 
                                gloveData.quaternion.z, 
                                gloveData.quaternion.w);
                        }
                        
                        // Update the hand model for this device
                        updateHandModel(deviceId);
                    }
                }
            } else {
                // If frames have the same timestamp, just use current frame
                applyFrame(currentFrame);
            }
        } else {
            // If this is the last frame, just apply it directly
            applyFrame(currentFrame);
        }
    }
}

function applyFrame(frame) {
    // Check if this is a legacy recording format (has joints array)
    if (frame.joints) {
        // Handle legacy format - apply to first connected glove if any
        const firstGloveId = Array.from(gloves.keys())[0];
        if (firstGloveId) {
            applyLegacyFrame(frame, firstGloveId);
        }
        return;
    }
    
    // Handle new format with device-specific data
    if (frame.gloves) {
        for (const [deviceId, gloveData] of Object.entries(frame.gloves)) {
            if (gloves.has(deviceId)) {
                const currentGloveData = gloves.get(deviceId);
                
                // Apply joint values from the frame
                for (let i = 0; i < Math.min(gloveData.jointValues.length, currentGloveData.jointValues.length); i++) {
                    currentGloveData.jointValues[i] = gloveData.jointValues[i];
                    
                    // Update the joint display
                    updateJointDisplay(deviceId, i, gloveData.jointValues[i]);
                }
                
                // Apply quaternion data
                if (gloveData.quaternion) {
                    currentGloveData.quaternion = { ...gloveData.quaternion };
                    updateQuaternionDisplay(deviceId, 
                        currentGloveData.quaternion.x, 
                        currentGloveData.quaternion.y, 
                        currentGloveData.quaternion.z, 
                        currentGloveData.quaternion.w);
                }
                
                // Update the hand model for this device
                updateHandModel(deviceId);
            }
        }
    }
}

// Helper function to handle legacy recording format
function applyLegacyFrame(frame, deviceId) {
    if (!gloves.has(deviceId)) return;
    
    const gloveData = gloves.get(deviceId);
    
    // Apply joint values from the legacy frame
    for (let i = 0; i < Math.min(frame.joints.length, gloveData.jointValues.length); i++) {
        gloveData.jointValues[i] = frame.joints[i];
        
        // Update the joint display
        updateJointDisplay(deviceId, i, frame.joints[i]);
    }
    
    // Update the hand model for this device
    updateHandModel(deviceId);
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
            filters: [
                { vendorId: EIDON_VENDOR_ID } // comment out to allow any device to be connected
            ]
        });

        for (const device of devices) {
            // Create a unique device ID by combining vendorId, productId, and the device index
            const baseDeviceId = getDeviceId(device);
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
            
            // Use cached colour (if any) for instant UI
            const cachedColor = getCachedColor(deviceId);

            if (device.productName.toLowerCase().includes('tracker')) {
                if (!trackers.has(deviceId)) addTrackerDisplay(deviceId, cachedColor);
            } else {
                addGloveDisplay(deviceId, cachedColor);
                createHandModel(deviceId);
            }

            // Read firmware colour, update UI and cache when it arrives
            const readColour = async () => {
                try {
                    const dv = await device.receiveFeatureReport(1);
                    const arr = new Uint8Array(dv.buffer);
                    if (arr.length >= 3) {
                        const offset = (arr[0] === 1 && arr.length >= 4) ? 1 : 0;
                        return (arr[offset] << 16) | (arr[offset + 1] << 8) | arr[offset + 2];
                    }
                } catch(err) {
                    console.warn('Colour report read failed', err);
                }
                return null;
            };

            readColour().then(col => {
                if (col === null) return;
                const arrow = trackerArrows.get(deviceId);
                const dotSelector = device.productName.toLowerCase().includes('tracker') ? 'tracker' : 'glove';
                const dot = document.querySelector(`#${dotSelector}-${deviceId} .device-dot`);
                if (arrow) {
                    arrow.color = col;
                    [arrow.forward.material, arrow.up.material].forEach(m=>m.color.setHex(col));
                }
                if (dot) dot.style.backgroundColor = '#' + col.toString(16).padStart(6,'0');
                setCachedColor(deviceId, col);
            });

            addLogMessage(`Connected to HID device: ${device.productName} (${deviceId})`);
            addLogMessage(`VendorID: 0x${device.vendorId.toString(16)}, ProductID: 0x${device.productId.toString(16)}`);
        }

        // Update UI
        updateConnectionStatus();
        
        // Update device grouping display if in chain mode
        if (chainMode) {
            repositionTrackerArrows();
        }

    } catch (error) {
        console.error('Error connecting to HID device:', error);
        addLogMessage(`Connection error: ${error.message}`);
    }
}

function getDeviceId(device) {
    return `${toHex(device.vendorId)}-${toHex(device.productId)}-${device.productName.replace(/\s+/g, '-').replace(/-+$/, '').toLowerCase()}`
}

// Update autoConnectToLastDevice to handle the new ID format
async function autoConnectToLastDevice() {
    const savedDevices = JSON.parse(localStorage.getItem('hidDevices') || '[]');
    // console.log('Saved devices for auto-connect:', savedDevices);
    
    if (savedDevices.length === 0) return;
    
    try {
        const devices = await navigator.hid.getDevices();
        // console.log('Available HID devices:', devices.map(d => ({
        //     vendorId: d.vendorId,
        //     productId: d.productId,
        //     productName: d.productName
        // })));
        
        // Create a map of available devices by their base ID
        const availableDevices = new Map();
        devices.forEach(device => {
            const baseId = getDeviceId(device);
            if (!availableDevices.has(baseId)) {
                availableDevices.set(baseId, []);
            }
            availableDevices.get(baseId).push(device);
        });
        
        // Create a map to track which devices have been matched
        const matchedDevices = new Set();
        
        // First pass: Try to match devices by their full ID
        for (const deviceId of savedDevices) {
            const [vendorId, productId] = deviceId.split('-').slice(0, 2);
            const baseId = deviceId;
            const matchingDevices = availableDevices.get(baseId) || [];
            
            // Try to find an exact match first
            const exactMatch = matchingDevices.find(d => 
                !matchedDevices.has(d) && 
                getDeviceId(d) === deviceId
            );
            
            if (exactMatch) {
                console.log('Exact match found:', exactMatch);
                matchedDevices.add(exactMatch);
                await connectDevice(exactMatch, deviceId);
                continue;
            }
        }
        
        // Second pass: Match remaining devices by type and order
        const remainingSavedDevices = savedDevices.filter(id => !Array.from(hidDevices.keys()).includes(id));
        const remainingAvailableDevices = devices.filter(d => !matchedDevices.has(d));
        
        // Group remaining devices by type (tracker vs glove)
        const savedTrackers = remainingSavedDevices.filter(id => id.toLowerCase().includes('tracker'));
        const savedGloves = remainingSavedDevices.filter(id => id.toLowerCase().includes('glove'));
        
        const availableTrackers = remainingAvailableDevices.filter(d => d.productName.toLowerCase().includes('tracker'));
        const availableGloves = remainingAvailableDevices.filter(d => d.productName.toLowerCase().includes('glove'));
        
        // Match trackers
        for (let i = 0; i < Math.min(savedTrackers.length, availableTrackers.length); i++) {
            await connectDevice(availableTrackers[i], savedTrackers[i]);
        }
        
        // Match gloves
        for (let i = 0; i < Math.min(savedGloves.length, availableGloves.length); i++) {
            await connectDevice(availableGloves[i], savedGloves[i]);
        }
        
        updateConnectionStatus();
            
    } catch (error) {
        console.error('Auto-connect error:', error);
        addLogMessage('Failed to auto-connect to saved devices');
    }
}

// Helper function to connect a device
async function connectDevice(device, deviceId) {
    try {
        await device.open();
        hidDevices.set(deviceId, device);
        device.addEventListener('inputreport', handleHIDInput);
        addLogMessage(`Auto-connected to HID device: ${device.productName} (${deviceId})`);

        const cachedColor = getCachedColor(deviceId);

        // Instant UI using cached colour
        if (device.productName.toLowerCase().includes('tracker')) {
            if (!trackers.has(deviceId)) addTrackerDisplay(deviceId, cachedColor);
        } else {
            addGloveDisplay(deviceId, cachedColor);
            createHandModel(deviceId);
        }

        const readColour = async () => {
            try {
                const dv = await device.receiveFeatureReport(1);
                const arr = new Uint8Array(dv.buffer);
                if (arr.length >= 3) {
                    const offset = (arr[0] === 1 && arr.length >= 4) ? 1 : 0;
                    return (arr[offset] << 16) | (arr[offset + 1] << 8) | arr[offset + 2];
                }
            } catch (err) {
                console.warn('Could not read colour feature from device', err);
            }
            return null;
        };

        // Fetch actual colour and update when available
        readColour().then(col => {
            if (col === null) return;
            const arrow = trackerArrows.get(deviceId);
            const dotSel = device.productName.toLowerCase().includes('tracker') ? 'tracker' : 'glove';
            const dot = document.querySelector(`#${dotSel}-${deviceId} .device-dot`);
            if (arrow) {
                arrow.color = col;
                [arrow.forward.material, arrow.up.material].forEach(m=>m.color.setHex(col));
            }
            if (dot) dot.style.backgroundColor = '#' + col.toString(16).padStart(6,'0');
            setCachedColor(deviceId, col);
        });

    } catch (error) {
        console.error(`Error connecting device ${deviceId}:`, error);
        addLogMessage(`Failed to connect to device: ${device.productName}`);
    }
}

// Update updateConnectionStatus to show more device details
function updateConnectionStatus() {
    if (hidDevices.size > 0) {
        statusIndicator.textContent = `Status: Connected to ${hidDevices.size} device(s)`;
        statusIndicator.className = 'status connected';
        connectButton.disabled = false;
        disconnectButton.disabled = false;
        disconnectButton.style.display = '';
    } else {
        statusIndicator.textContent = 'Status: Disconnected';
        statusIndicator.className = 'status disconnected';
        connectButton.disabled = false;
        disconnectButton.disabled = true;
        disconnectButton.style.display = 'none';
    }
}

let firstFrame = true;

const RAD_TO_DEG = 180 / Math.PI;

// Update quaternion to Euler conversion to match firmware implementation
function quaternionToEuler(x, y, z, w) {
    // Different quaternion to euler conversion that might reduce axis coupling
    const yaw = Math.atan2(2.0 * (w * z + x * y),
                          1.0 - 2.0 * (y * y + z * z));
    
    const roll = Math.asin(2.0 * (w * y - z * x));
    
    const pitch = Math.atan2(2.0 * (w * x + y * z),
                           1.0 - 2.0 * (x * x + y * y));

    // Convert to degrees and swap pitch and roll
    return {
        yaw: yaw * RAD_TO_DEG,
        roll: roll * RAD_TO_DEG,
        pitch: pitch * RAD_TO_DEG
    };
}

// Modify handleHIDInput to extract arm position bits
function handleHIDInput(event) {
    if (ignoreExternalInput) return;

    const device = event.device;
    const { data } = event;

    // Validate device information
    // if (!device || !device.vendorId || !device.productId || !device.productName) {
    //     console.error('Invalid device information:', device);
    //     return;
    // }

    const deviceId = getDeviceId(device);
    // console.log('HID Input - Device:', {
    //     vendorId: device.vendorId,
    //     productId: device.productId,
    //     productName: device.productName,
    //     deviceId: deviceId
    // });
    
    // Determine if this is a tracker or glove based on report size
    const isTracker = data.buffer.byteLength === TRACKER_REPORT_SIZE;

    if (isTracker) {
        // Handle tracker data
        if (!trackers.has(deviceId)) {
            console.log('Creating new tracker display for:', deviceId);
            // Remove any existing glove displays
            const existingGloves = document.querySelectorAll('.glove-info');
            existingGloves.forEach(glove => glove.remove());
            
            // Add tracker display
            addTrackerDisplay(deviceId);
            
            // Re-add any existing glove displays
            for (const [gloveId, gloveData] of gloves) {
                addGloveDisplay(gloveId);
            }
        }
        
        // Handle tracker data - reading little-endian 16-bit integers
        // Each quaternion component is stored as two bytes in little-endian format
        const x = ((data.getUint8(0) | (data.getUint8(1) << 8)) - 32768) / 32767.5;
        const y = ((data.getUint8(2) | (data.getUint8(3) << 8)) - 32768) / 32767.5;
        const z = ((data.getUint8(4) | (data.getUint8(5) << 8)) - 32768) / 32767.5;
        const w = ((data.getUint8(6) | (data.getUint8(7) << 8)) - 32768) / 32767.5;
        
        // Extract arm position information from the last byte (index 8)
        const configByte = data.getUint8(8);
        const isRightArm = (configByte & 0x01) === 0; // Bit 0: 0 = right, 1 = left
        const isLowerArm = (configByte & 0x02) === 0; // Bit 1: 0 = upper, 1 = lower
        
        // Get tracker data structure
        const tracker = trackers.get(deviceId);
        
        // Update arm position info if changed
        if (tracker.isRightArm !== isRightArm || tracker.isLowerArm !== isLowerArm) {
            tracker.isRightArm = isRightArm;
            tracker.isLowerArm = isLowerArm;
            
            // Update the display to show the new arm position
            updateTrackerArmPosition(deviceId, isRightArm, isLowerArm);
        }
        
        // Update tracker display with quaternion values
        updateTrackerDisplay(deviceId, x, y, z, w);
    } else {
        // Glove handling
        if (!gloves.has(deviceId)) {
            addGloveDisplay(deviceId);
            createHandModel(deviceId); // Create 3D hand model for this device
        }

        const gloveData = gloves.get(deviceId);
        let hasChanges = false;
        
        // Extract left/right hand information from second byte after reportId
        // ReportId is byte 0, so the hand info is in byte 1
        const configByte = data.getUint8(1);
        const isRightHand = (configByte & 0x01) === 0; // Bit 0: 0 = right, 1 = left

        // Update hand position if changed
        if (gloveData.isRightHand !== isRightHand) {
            gloveData.isRightHand = isRightHand;
            
            // Update the display to show hand position
            updateGloveHandPosition(deviceId, isRightHand);
        }

        // Detect button 8 press (bit 7 of configByte)
        const buttonsByte = data.getUint8(0);
        const buttonMask = buttonsByte & 0xFF; // bits 0-7 correspond to buttons 1-8
        const BUTTON_1_MASK = 0x01;
        if ((buttonMask & BUTTON_1_MASK) && !(gloveData.prevButtonState & BUTTON_1_MASK)) {
            // Button 8 was just pressed – trigger calibration on same-arm trackers
            calibrateTrackersOnSameArm(deviceId);
        }
        // Store current button state for next comparison
        gloveData.prevButtonState = buttonMask;
        
        // Process joint values
        for (let i = 0; i < 16; i++) {
            const rawValue = data.getUint8(i + 2);
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
        const quaternionX = ((data.getUint8(18) | (data.getUint8(19) << 8)) - 32768) / 32767.5;
        const quaternionY = ((data.getUint8(20) | (data.getUint8(21) << 8)) - 32768) / 32767.5;
        const quaternionZ = ((data.getUint8(22) | (data.getUint8(23) << 8)) - 32768) / 32767.5;
        const quaternionW = ((data.getUint8(24) | (data.getUint8(25) << 8)) - 32768) / 32767.5;

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

// Add a helper function to update the tracker arm position display
function updateTrackerArmPosition(deviceId, isRightArm, isLowerArm) {
    const trackerElement = document.getElementById(`tracker-${deviceId}`);
    if (!trackerElement) return;
    
    // Find or create the arm position span
    let armPositionSpan = document.getElementById(`arm-position-${deviceId}`);
    if (!armPositionSpan) {
        const trackerControls = trackerElement.querySelector('.tracker-controls');
        if (trackerControls) {
            // Create position info with appropriate emoji and text
            armPositionSpan = document.createElement('span');
            armPositionSpan.id = `arm-position-${deviceId}`;
            armPositionSpan.className = 'device-arm-position';
            trackerControls.append(armPositionSpan);
        }
    }
    
    if (armPositionSpan) {
        // Determine side and position emoji
        const sideEmoji = isRightArm ? '👉' : '👈';
        const positionEmoji = isLowerArm ? '💪' : '🦾';
        
        // Determine position text
        const positionText = isLowerArm ? 'Lower' : 'Upper';
        const sideText = isRightArm ? 'Right' : 'Left';
        
        // Update text content with emoji
        armPositionSpan.innerHTML = `${positionEmoji} ${sideText} ${positionText}`;
        
        // Update the tracker object name
        // const nameElement = trackerElement.querySelector('.tracker-name');
        // if (nameElement) {
        //     const deviceNameSpan = nameElement.querySelector('.device-name') || document.createElement('span');
        //     if (!deviceNameSpan.classList.contains('device-name')) {
        //         deviceNameSpan.className = 'device-name';
        //         // Extract the dot and text
        //         const dot = nameElement.querySelector('.device-dot');
        //         const text = nameElement.childNodes[2]; // Text node after the dot
                
        //         // Clear the name element
        //         nameElement.innerHTML = '';
                
        //         // Re-add components
        //         if (dot) nameElement.appendChild(dot);
        //         nameElement.appendChild(deviceNameSpan);
        //     }
            
        //     // Get device from map
        //     const device = hidDevices.get(deviceId);
        //     const deviceName = device ? device.productName : 'Tracker';
            
        //     // Create a descriptive name
        //     deviceNameSpan.textContent = `${deviceName} (${sideText} ${positionText})`;
        // }
    }
}

// Modify addTrackerDisplay to initialize arm position
function addTrackerDisplay(deviceId, presetColor = null) {
    console.log(`Adding tracker display for deviceId: ${deviceId}`);
    const device = hidDevices.get(deviceId);
    console.log('Device from hidDevices:', device);
    console.log('Device productName:', device?.productName);
    
    const trackerElement = document.createElement('div');
    trackerElement.className = 'tracker-info';
    trackerElement.id = `tracker-${deviceId}`;
    
    // Get device name from hidDevices
    const deviceName = device ? device.productName : 'Tracker';
    console.log('Final deviceName:', deviceName);
    
    // Get color for the dot
    let color = presetColor !== null ? presetColor : getColorFromDeviceName(deviceId);
    const colorHex = color ? '#' + color.toString(16).padStart(6, '0') : '#ffffff';
    
    trackerElement.innerHTML = `
        <div class="tracker-header">
            <div class="tracker-name">
                <span class="device-dot" style="background-color: ${colorHex}"></span>
                <span class="device-name">${deviceName}</span>
            </div>
            <div class="tracker-details">
                <span class="device-id">ID: ${deviceId}</span>
            </div>
            <div class="tracker-controls">
                <button class="calibrate-btn" onclick="calibrateDevice('${deviceId}')">Calibrate</button>
                <button class="disconnect-btn" onclick="disconnectFromDevice('${deviceId}')">Disconnect</button>
            </div>
        </div>
        <div class="tracker-values">
            <div class="tracker-value-label">🌐 Orientation</div>
            <div class="quaternion-values">
                <div>X<br><span id="tracker-quat-x-${deviceId}">0.000</span></div>
                <div>Y<br><span id="tracker-quat-y-${deviceId}">0.000</span></div>
                <div>Z<br><span id="tracker-quat-z-${deviceId}">0.000</span></div>
                <div>W<br><span id="tracker-quat-w-${deviceId}">0.000</span></div>
            </div>
            <div class="euler-values">
                <div class="tracker-value-container">
                    <div class="tracker-value-label">Roll:</div>
                    <div class="tracker-circle-container">
                        <div class="tracker-circle" id="tracker-circle-roll-${deviceId}">
                            <div class="tracker-indicator"></div>
                        </div>
                        <span id="tracker-roll-${deviceId}">0.0°</span>
                    </div>
                </div>
                <div class="tracker-value-container">
                    <div class="tracker-value-label">Pitch:</div>
                    <div class="tracker-circle-container">
                        <div class="tracker-circle" id="tracker-circle-pitch-${deviceId}">
                            <div class="tracker-indicator"></div>
                        </div>
                        <span id="tracker-pitch-${deviceId}">0.0°</span>
                    </div>
                </div>
                <div class="tracker-value-container">
                    <div class="tracker-value-label">Yaw:</div>
                    <div class="tracker-circle-container">
                        <div class="tracker-circle" id="tracker-circle-yaw-${deviceId}">
                            <div class="tracker-indicator"></div>
                        </div>
                        <span id="tracker-yaw-${deviceId}">0.0°</span>
                    </div>
                </div>
            </div>
        </div>
    `;
    
    // Add to joints container
    jointsContainer.appendChild(trackerElement);
    
    // Add to trackers Map with quaternion values and default arm position values
    trackers.set(deviceId, {
        quaternion: { x: 0, y: 0, z: 0, w: 1 },
        euler: { roll: 0, pitch: 0, yaw: 0 },
        isRightArm: null, // Default to left arm
        isLowerArm: null  // Default to upper arm
    });

    // Create tracker arrow in Three.js scene with chosen colour
    createTrackerArrow(deviceId, color);

    // After arrow creation (or if colour came from device) paint dot & arrow
    const arrowInfo = trackerArrows.get(deviceId);
    const dotEl = trackerElement.querySelector('.device-dot');
    if (arrowInfo && dotEl) {
        dotEl.style.backgroundColor = '#' + arrowInfo.color.toString(16).padStart(6, '0');
        // Add click handler to change colour
        attachColorPicker(dotEl, deviceId);
        return; // skip legacy code
    }

    // Rest of the function remains unchanged...
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
            <div class="joint-value" id="joint-value-${i}">0</div>
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
        <div class="joint-name">🌐 Orientation</div>
        <div class="quaternion-values">
            <div>X<br><span id="quat-x">0.000</span></div>
            <div>Y<br><span id="quat-y">0.000</span></div>
            <div>Z<br><span id="quat-z">0.000</span></div>
            <div>W<br><span id="quat-w">0.000</span></div>
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
        valueElement.textContent = `${value}`;
        
        const jointInfo = fingerJointMap[jointIndex];
        const min = jointInfo?.min || 0;
        const max = jointInfo?.max || 255;
        const range = max - min;
        
        const percentage = Math.min(100, Math.max(0, ((value - min) / range) * 100));
        barElement.style.height = `${percentage}%`;
        
        const hue = Math.floor(percentage * 1.2);
        barElement.style.backgroundColor = `hsl(${hue}, 80%, 50%)`;
    }
}

// Add to the end of the file or where other initialization code is
// Try to auto-connect when the page loads
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function() {
        // Initialize Three.js
        initThreeJS();
        
        // Initialize hand model visibility
        initializeHandModelVisibility();
        
        // Auto-connect to previously connected devices
        autoConnectToLastDevice();
        
        // Initialize default hands
        // initializeDefaultHands();
    });
} else {
    // Page already loaded
    autoConnectToLastDevice();
    // displayDeviceGrouping(); // Initialize device grouping display
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

// Modify updateTrackerDisplay function
function updateTrackerDisplay(deviceId, x, y, z, w) {
    // Store quaternion values
    const tracker = trackers.get(deviceId);
    tracker.quaternion = { x, y, z, w };
    
    // Calculate Euler angles from quaternion
    const euler = quaternionToEuler(x, y, z, w);
    tracker.euler = euler;
    
    // Update quaternion display values
    document.getElementById(`tracker-quat-x-${deviceId}`).textContent = x.toFixed(3);
    document.getElementById(`tracker-quat-y-${deviceId}`).textContent = y.toFixed(3);
    document.getElementById(`tracker-quat-z-${deviceId}`).textContent = z.toFixed(3);
    document.getElementById(`tracker-quat-w-${deviceId}`).textContent = w.toFixed(3);
    
    // Update Euler angle displays (euler values are already in degrees)
    document.getElementById(`tracker-roll-${deviceId}`).textContent = `${euler.roll.toFixed(1)}°`;
    document.getElementById(`tracker-pitch-${deviceId}`).textContent = `${euler.pitch.toFixed(1)}°`;
    document.getElementById(`tracker-yaw-${deviceId}`).textContent = `${euler.yaw.toFixed(1)}°`;
    
    // Update circular indicators (euler values are already in degrees)
    const updateCircle = (id, value) => {
        const circle = document.getElementById(id);
        if (circle) {
            // Convert degrees to radians for the circle rotation
            const rotation = (value % 360) * (Math.PI / 180);
            const indicator = circle.querySelector('.tracker-indicator');
            if (indicator) {
                // Calculate the position of the indicator on the circle
                const circleSize = circle.offsetWidth;
                const indicatorSize = 8; // Size of the indicator dot
                const radius = (circleSize - indicatorSize) / 2; // Adjust radius to account for indicator size
                
                // Calculate position with indicator size offset
                const x = Math.sin(rotation) * radius;
                const y = -Math.cos(rotation) * radius; // Negative because Y is inverted in CSS
                
                // Apply the transform, adjusting for the indicator's center point
                indicator.style.transform = `translate(calc(${x}px - ${indicatorSize/2}px), calc(${y}px - ${indicatorSize/2}px))`;
            }
        }
    };
    
    updateCircle(`tracker-circle-roll-${deviceId}`, euler.roll);
    updateCircle(`tracker-circle-pitch-${deviceId}`, euler.pitch);
    updateCircle(`tracker-circle-yaw-${deviceId}`, euler.yaw);

    // Update the tracker arrow in Three.js scene
    updateTrackerArrow(deviceId, tracker.quaternion);
}

// Add this function to create a glove display section
function addGloveDisplay(deviceId, presetColor = null) {
    // If already exists, just update color and return
    const existing = document.getElementById(`glove-${deviceId}`);
    if (existing) {
        const arrow = trackerArrows.get(deviceId);
        const dot = existing.querySelector('.device-dot');
        const col = presetColor !== null ? presetColor : (arrow ? arrow.color : null);
        if (col !== null && dot) {
            dot.style.backgroundColor = '#' + col.toString(16).padStart(6,'0');
        }
        if (!trackerArrows.has(deviceId)) {
            createTrackerArrow(deviceId, col);
        } else if (col !== null) {
            const arr = trackerArrows.get(deviceId);
            arr.color = col;
            [arr.forward.material, arr.up.material].forEach(m=>m.color.setHex(col));
        }
        attachColorPicker(dot, deviceId);
        return;
    }

    const gloveId = deviceId.split('-').pop(); // Get unique part of device ID
    const gloveElement = document.createElement('div');
    gloveElement.className = 'glove-info';
    gloveElement.id = `glove-${deviceId}`;
    
    // Get device name from hidDevices
    const device = hidDevices.get(deviceId);
    const deviceName = device ? device.productName : 'Glove';
    
    // Get color for the dot
    let color = presetColor !== null ? presetColor : getColorFromDeviceName(deviceId);
    const colorHex = color ? '#' + color.toString(16).padStart(6, '0') : '#ffffff';
    
    // Create glove header
    const header = document.createElement('div');
    header.className = 'glove-header';
    header.innerHTML = `
        <div class="glove-name">
            <span class="device-dot" style="background-color: ${colorHex}"></span>
            <span class="device-name">${deviceName}</span>
        </div>
        <div class="glove-details">
            <span class="device-id">ID: ${deviceId}</span>
        </div>
        <div class="glove-controls">
            <button class="calibrate-btn" onclick="calibrateDevice('${deviceId}')">Calibrate</button>
            <button class="disconnect-btn" onclick="disconnectFromDevice('${deviceId}')">Disconnect</button>
        </div>
    `;
    gloveElement.appendChild(header);

    // Create joints container for this glove
    const glovejointsContainer = document.createElement('div');
    glovejointsContainer.className = 'glove-joints-container';
    
    // Add quaternion display for this glove first
    const quaternionElement = document.createElement('div');
    quaternionElement.className = 'joint-info';
    quaternionElement.innerHTML = `
        <div class="joint-name">🌐 Orientation</div>
        <div class="quaternion-values">
            <div>X<br><span id="quat-x-${deviceId}">0.000</span></div>
            <div>Y<br><span id="quat-y-${deviceId}">0.000</span></div>
            <div>Z<br><span id="quat-z-${deviceId}">0.000</span></div>
            <div>W<br><span id="quat-w-${deviceId}">0.000</span></div>
        </div>
        <div class="euler-values">
            <div class="tracker-value-container">
                <div class="tracker-value-label">Roll:</div>
                <div class="tracker-circle-container">
                    <div class="tracker-circle" id="glove-circle-roll-${deviceId}">
                        <div class="tracker-indicator"></div>
                    </div>
                    <span id="glove-roll-${deviceId}">0.0°</span>
                </div>
            </div>
            <div class="tracker-value-container">
                <div class="tracker-value-label">Pitch:</div>
                <div class="tracker-circle-container">
                    <div class="tracker-circle" id="glove-circle-pitch-${deviceId}">
                        <div class="tracker-indicator"></div>
                    </div>
                    <span id="glove-pitch-${deviceId}">0.0°</span>
                </div>
            </div>
            <div class="tracker-value-container">
                <div class="tracker-value-label">Yaw:</div>
                <div class="tracker-circle-container">
                    <div class="tracker-circle" id="glove-circle-yaw-${deviceId}">
                        <div class="tracker-indicator"></div>
                    </div>
                    <span id="glove-yaw-${deviceId}">0.0°</span>
                </div>
            </div>
        </div>
    `;
    glovejointsContainer.appendChild(quaternionElement);
    
    // Create new compact angles display section
    const anglesElement = document.createElement('div');
    anglesElement.className = 'joint-info compact-angles';
    anglesElement.innerHTML = `
        <div class="joint-name">🖐 Finger Angles</div>
        <div class="compact-angles-container" id="compact-angles-${deviceId}"></div>
    `;
    glovejointsContainer.appendChild(anglesElement);
    
    // Add compact angles container to the glove joints container
    gloveElement.appendChild(glovejointsContainer);
    
    // Add to joints container
    jointsContainer.appendChild(gloveElement);
    
    // Initialize glove data in the Map
    gloves.set(deviceId, {
        jointValues: new Array(MAX_JOINTS).fill(0),
        jointInversions: new Array(MAX_JOINTS).fill(false),
        quaternion: { x: 0, y: 0, z: 0, w: 1 },
        euler: { roll: 0, pitch: 0, yaw: 0 },
        isRightHand: false, // Default to left hand
        prevButtonState: 0 // Track previous button state for press detection
    });

    // Create tracker arrow for the glove with chosen colour
    createTrackerArrow(deviceId, color);

    // Attach colour picker to dot and sync initial colour
    const gloveDot = gloveElement.querySelector('.device-dot');
    if (gloveDot) {
        const arrowInfo = trackerArrows.get(deviceId);
        if (arrowInfo) {
            gloveDot.style.backgroundColor = '#' + arrowInfo.color.toString(16).padStart(6, '0');
        }
        attachColorPicker(gloveDot, deviceId);
    }
    
    // Display initial hand position
    updateGloveHandPosition(deviceId, false); // Default to left hand
    
    // Create the compact angle bars with tooltips, grouped by finger
    const compactAnglesContainer = document.getElementById(`compact-angles-${deviceId}`);
    if (compactAnglesContainer) {
        // Define finger groups
        const fingerGroups = [
            { name: 'Thumb', startIndex: 0, count: 4 },
            { name: 'Index', startIndex: 4, count: 3 },
            { name: 'Middle', startIndex: 7, count: 3 },
            { name: 'Ring', startIndex: 10, count: 3 },
            { name: 'Pinky', startIndex: 13, count: 3 }
        ];
        
        // Create a container for each finger group
        fingerGroups.forEach(group => {
            const groupContainer = document.createElement('div');
            groupContainer.className = 'finger-group';
            
            // Add finger group label
            const groupLabel = document.createElement('div');
            groupLabel.className = 'finger-group-label';
            groupLabel.textContent = group.name;
            groupContainer.appendChild(groupLabel);
            
            // Create content container for the bars
            const contentContainer = document.createElement('div');
            contentContainer.className = 'finger-group-content';
            
            // Create bars for this finger group
            for (let i = 0; i < group.count; i++) {
                const jointIndex = group.startIndex + i;
                const jointType = fingerJointMap[jointIndex]?.type || 'Unknown';
                
                const barContainer = document.createElement('div');
                barContainer.className = 'compact-angle-item';
                barContainer.title = `${group.name} - ${jointType}`;
                
                const bar = document.createElement('div');
                bar.className = 'compact-angle-bar';
                bar.id = `joint-bar-${deviceId}-${jointIndex}`;
                
                const valueSpan = document.createElement('span');
                valueSpan.className = 'compact-angle-value';
                valueSpan.id = `joint-value-${deviceId}-${jointIndex}`;
                valueSpan.textContent = '0';
                
                barContainer.appendChild(bar);
                barContainer.appendChild(valueSpan);
                contentContainer.appendChild(barContainer);
            }
            
            groupContainer.appendChild(contentContainer);
            compactAnglesContainer.appendChild(groupContainer);
        });
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
    document.getElementById(`glove-roll-${deviceId}`).textContent = `${(euler.roll).toFixed(1)}°`;
    document.getElementById(`glove-pitch-${deviceId}`).textContent = `${(euler.pitch).toFixed(1)}°`;
    document.getElementById(`glove-yaw-${deviceId}`).textContent = `${(euler.yaw).toFixed(1)}°`;
    
    // Update circle indicators
    updateCircleIndicator(`glove-circle-roll-${deviceId}`, euler.roll);
    updateCircleIndicator(`glove-circle-pitch-${deviceId}`, euler.pitch);
    updateCircleIndicator(`glove-circle-yaw-${deviceId}`, euler.yaw);
    
    // Update tracker arrow using the same function as trackers
    updateTrackerArrow(deviceId, { x, y, z, w });
    
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

// Function to compute wrist flexion/extension angle
function computeWristAngle(qWrist, qHand) {
    // 1. Compute relative quaternion (hand relative to wrist)
    const qRelative = qWrist.clone().invert().multiply(qHand);
    
    // 2. Extract the flexion angle directly from the quaternion
    // Wrist flexion is primarily around the X-axis in the local wrist frame
    // We'll use a more direct approach to extract this angle
    
    // Convert quaternion to Euler angles (in radians)
    const euler = new THREE.Euler().setFromQuaternion(qRelative);
    
    // Extract the X rotation (flexion/extension)
    // Convert to degrees and apply a scaling factor if needed
    const flexionAngle = THREE.MathUtils.radToDeg(euler.x);
    
    // Apply a scaling factor to make the movement more pronounced or subtle
    // Adjust this value based on your preference (1.0 is no scaling)
    const scalingFactor = 1.5;
    
    // Apply the scaling factor
    const scaledAngle = flexionAngle * scalingFactor;
    
    // Log the raw and scaled angles for debugging
    console.log(`Raw flexion angle: ${flexionAngle.toFixed(2)}°, Scaled: ${scaledAngle.toFixed(2)}°`);
    
    return scaledAngle;
}

// Function to compute radial/ulnar deviation angle
function computeWristDeviation(qWrist, qHand) {
    // 1. Compute relative quaternion (hand relative to wrist)
    const qRelative = qWrist.clone().invert().multiply(qHand);
    
    // 2. Extract the deviation angle from the quaternion
    // Radial/ulnar deviation is primarily around the Z-axis in the local wrist frame
    
    // Convert quaternion to Euler angles (in radians)
    const euler = new THREE.Euler().setFromQuaternion(qRelative);
    
    // Extract the Z rotation (radial/ulnar deviation)
    // Convert to degrees
    const deviationAngle = THREE.MathUtils.radToDeg(euler.z);
    
    // Apply a scaling factor to make the movement more pronounced or subtle
    const scalingFactor = 1.5;
    
    // Apply the scaling factor
    const scaledAngle = deviationAngle * scalingFactor;
    
    // Log the raw and scaled angles for debugging
    console.log(`Raw deviation angle: ${deviationAngle.toFixed(2)}°, Scaled: ${scaledAngle.toFixed(2)}°`);
    
    return scaledAngle;
}

// Add new function to update arm position based on IMU sensors
// function updateArmPosition(deviceId) {
//     const handModel = hands.get(deviceId);
//     if (!handModel) return;

//     const gloveData = gloves.get(deviceId);
//     const wristTracker = trackers.get(`0-0-Eidon-Tracker-1`);

//     if (!gloveData || !wristTracker) return;

//     // Get quaternion values from wrist tracker and hand
//     const wristQuat = wristTracker.quaternion;
//     const handQuat = gloveData.quaternion;

//     // Set fixed arm positions
//     handModel.arm.elbow.rotation.y = THREE.MathUtils.degToRad(0);
//     handModel.arm.shoulder.rotation.x = THREE.MathUtils.degToRad(90);
//     handModel.arm.shoulder.rotation.y = THREE.MathUtils.degToRad(180);
    
//     // Create THREE.Quaternion objects
//     const qWrist = new THREE.Quaternion(
//         wristQuat.x,
//         wristQuat.y,
//         wristQuat.z,
//         wristQuat.w
//     );
    
//     const qHand = new THREE.Quaternion(
//         handQuat.x,
//         handQuat.y,
//         handQuat.z,
//         handQuat.w
//     );
    
//     // Extract forearm roll from wrist tracker
//     // This is the rotation around the forearm's long axis
//     const forearmQuaternion = qWrist.clone();
//     const forearmRoll = Math.atan2(2 * (forearmQuaternion.w * forearmQuaternion.z + forearmQuaternion.x * forearmQuaternion.y),
//                                    1 - 2 * (forearmQuaternion.y * forearmQuaternion.y + forearmQuaternion.z * forearmQuaternion.z)) * (180 / Math.PI);
    
//     // Apply forearm roll to the forearm
//     handModel.arm.forearm.rotation.y = THREE.MathUtils.degToRad(-forearmRoll + 45);
    
//     // Compute wrist flexion/extension angle
//     const wristAngle = computeWristAngle(qWrist, qHand);
    
//     // Apply the wrist angle to the wrist's x rotation
//     handModel.arm.wrist.rotation.x = THREE.MathUtils.degToRad(-wristAngle + 90);
    
//     // Compute radial/ulnar deviation angle (but don't apply it yet)
//     const deviationAngle = computeWristDeviation(qWrist, qHand);
    
//     // Reset other wrist rotations
//     handModel.arm.wrist.rotation.y = 0;
//     handModel.arm.wrist.rotation.z = 0;
    
//     // Log the values for debugging
//     console.log(`Forearm Roll: ${forearmRoll.toFixed(2)}°, Wrist Flexion: ${wristAngle.toFixed(2)}°, Radial/Ulnar Deviation: ${deviationAngle.toFixed(2)}°`);
// }

function updateGloveDisplay(deviceId, data) {
    const gloveData = gloves.get(deviceId);
    if (!gloveData) return;

    // Update joint values
    for (let i = 0; i < MAX_JOINTS; i++) {
        const value = data.jointValues[i];
        const inverted = gloveData.jointInversions[i];
        const displayValue = inverted ? 1 - value : value;
        
        // Update joint value display - now just the number
        const valueElement = document.getElementById(`joint-value-${deviceId}-${i}`);
        if (valueElement) {
            valueElement.textContent = displayValue;
        }
        
        // Update joint bar - compact version
        const barElement = document.getElementById(`joint-bar-${deviceId}-${i}`);
        if (barElement) {
            const jointInfo = fingerJointMap[i];
            const min = jointInfo?.min || 0;
            const max = jointInfo?.max || 255;
            const range = max - min;
            
            const percentage = Math.min(100, Math.max(0, ((displayValue - min) / range) * 100));
            barElement.style.height = `${percentage}%`;  // Now using height instead of width
            
            const hue = Math.floor(percentage * 1.2);
            barElement.style.backgroundColor = `hsl(${hue}, 80%, 50%)`;
        }
    }

    // Update quaternion values
    if (data.quaternion) {
        gloveData.quaternion = data.quaternion;
        
        // Update quaternion text values
        document.getElementById(`quat-x-${deviceId}`).textContent = data.quaternion.x.toFixed(3);
        document.getElementById(`quat-y-${deviceId}`).textContent = data.quaternion.y.toFixed(3);
        document.getElementById(`quat-z-${deviceId}`).textContent = data.quaternion.z.toFixed(3);
        document.getElementById(`quat-w-${deviceId}`).textContent = data.quaternion.w.toFixed(3);

        // Calculate Euler angles from quaternion
        const euler = quaternionToEuler(data.quaternion);
        gloveData.euler = euler;

        // Update Euler angle displays
        document.getElementById(`glove-roll-${deviceId}`).textContent = `${euler.roll.toFixed(1)}°`;
        document.getElementById(`glove-pitch-${deviceId}`).textContent = `${euler.pitch.toFixed(1)}°`;
        document.getElementById(`glove-yaw-${deviceId}`).textContent = `${euler.yaw.toFixed(1)}°`;

        // Update circle indicators
        updateCircleIndicator(`glove-circle-roll-${deviceId}`, euler.roll);
        updateCircleIndicator(`glove-circle-pitch-${deviceId}`, euler.pitch);
        updateCircleIndicator(`glove-circle-yaw-${deviceId}`, euler.yaw);
    }
}

function updateCircleIndicator(circleId, angle) {
    const circle = document.getElementById(circleId);
    if (!circle) return;

    const indicator = circle.querySelector('.tracker-indicator');
    if (!indicator) return;

    // Convert degrees to radians for the circle rotation
    const rotation = (angle % 360) * (Math.PI / 180);
    
    // Calculate the position of the indicator on the circle
    const circleSize = circle.offsetWidth;
    const indicatorSize = 8; // Size of the indicator dot
    const radius = (circleSize - indicatorSize) / 2; // Adjust radius to account for indicator size
    
    // Calculate position with indicator size offset
    const x = Math.sin(rotation) * radius;
    const y = -Math.cos(rotation) * radius; // Negative because Y is inverted in CSS
    
    // Apply the transform, adjusting for the indicator's center point
    indicator.style.transform = `translate(calc(${x}px - ${indicatorSize/2}px), calc(${y}px - ${indicatorSize/2}px))`;
}

// Assuming q1 and q2 are quaternions representing the orientation of two IMUs
// q1 is the "parent" IMU (closer to the body)
// q2 is the "child" IMU (further from the body)

// To find the relative orientation of q2 with respect to q1:
// q_relative = q2 * q1^(-1)
// where q1^(-1) is the inverse (conjugate) of q1

function calculateRelativeOrientation(q1, q2) {
    // Normalize quaternions to ensure they're unit quaternions
    q1 = normalizeQuaternion(q1);
    q2 = normalizeQuaternion(q2);
    
    // Calculate the inverse (conjugate) of q1
    const q1Inverse = {
        w: q1.w,
        x: -q1.x,
        y: -q1.y,
        z: -q1.z
    };
    
    // Multiply q2 by q1's inverse to get the relative orientation
    return multiplyQuaternions(q2, q1Inverse);
}

// Helper function to normalize a quaternion
function normalizeQuaternion(q) {
    const magnitude = Math.sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    return {
        w: q.w / magnitude,
        x: q.x / magnitude,
        y: q.y / magnitude,
        z: q.z / magnitude
    };
}

// Helper function to multiply two quaternions
function multiplyQuaternions(q1, q2) {
    return {
        w: q1.w*q2.w - q1.x*q2.x - q1.y*q2.y - q1.z*q2.z,
        x: q1.w*q2.x + q1.x*q2.w + q1.y*q2.z - q1.z*q2.y,
        y: q1.w*q2.y - q1.x*q2.z + q1.y*q2.w + q1.z*q2.x,
        z: q1.w*q2.z + q1.x*q2.y - q1.y*q2.x + q1.z*q2.w
    };
}

function quaternionToJointAngles(q) {
    // This function converts a quaternion to a set of joint angles
    // The exact implementation depends on your joint's degrees of freedom
    
    // For a simple hinge joint (like the elbow), you might only need one angle
    // For a ball joint (like the shoulder), you might need three angles
    
    // Example for a hinge joint (simplified):
    const angle = 2 * Math.acos(q.w);
    
    // Example for a ball joint (three angles):
    const roll = Math.atan2(2*(q.w*q.x + q.y*q.z), 1 - 2*(q.x*q.x + q.y*q.y));
    const pitch = Math.asin(2*(q.w*q.y - q.z*q.x));
    const yaw = Math.atan2(2*(q.w*q.z + q.x*q.y), 1 - 2*(q.y*q.y + q.z*q.z));
    
    return {
        angle: angle * (180/Math.PI), // Convert to degrees
        roll: roll * (180/Math.PI),
        pitch: pitch * (180/Math.PI),
        yaw: yaw * (180/Math.PI)
    };
}

// Add this function before createTrackerArrow
function getColorFromDeviceName(deviceName) {
    // Special case for gloves - always use a specific color
    if (deviceName.toLowerCase().includes('glove')) {
        return 0xFFFFFF; // Use white color for gloves
    }

    const colorMap = {
        'red': 0xff0000,
        'green': 0x61c680,
        'blue': 0x0000ff,
        'yellow': 0xffff00,
        'cyan': 0x00ffff,
        'magenta': 0xff00ff,
        'white': 0xffffff,
        'black': 0x000000,
        'orange': 0xfa9863,  // Changed to mandarin orange
        'purple': 0x800080,
        'pink': 0xffc0cb
    };

    // Convert device name to lowercase for case-insensitive matching
    const lowerName = deviceName.toLowerCase();
    
    // Check if any color name is in the device name
    for (const [colorName, colorValue] of Object.entries(colorMap)) {
        if (lowerName.includes(colorName)) {
            return colorValue;
        }
    }
    
    // If no color found, return null to use default color scheme
    return null;
}

function createTrackerArrow(deviceId, presetColor=null) {
    // If arrow already exists, optionally update colour and exit
    if (trackerArrows.has(deviceId)) {
        const existing = trackerArrows.get(deviceId);
        if (presetColor !== null) {
            existing.color = presetColor;
            [existing.forward.material, existing.up.material].forEach(m=>m.color.setHex(presetColor));
        }
        return existing;
    }

    // Create cylinder geometry for the forward arrow shaft
    const radius = 0.1; // Thickness of the arrow
    const height = 1;   // Initial height (will be scaled)
    const geometry = new THREE.CylinderGeometry(radius, radius, height, 8);
    
    // Try to get color from device name - check the entire device ID
    let color = presetColor !== null ? presetColor : getColorFromDeviceName(deviceId);
    
    // If no color found in name, use default color scheme
    if (color === null) {
    const colors = [
        0xff00ff, // Magenta
        0x00ffff, // Cyan
        0xff0000, // Red
        0x0000ff, // Blue
        0x61c680, // Green
        0xffff00, // Yellow
    ];
    const index = Array.from(trackerArrows.keys()).length % colors.length;
        color = colors[index];
    }
    
    // Create materials
    const forwardMaterial = new THREE.MeshBasicMaterial({ color: color });
    const upMaterial = new THREE.MeshBasicMaterial({ color: color, opacity: 0.9, transparent: true });
    const projectedMaterial = new THREE.MeshBasicMaterial({ color: 0xffffff, opacity: 0.2, transparent: true }); // Default white, will be updated
    const invertedUpMaterial = new THREE.MeshBasicMaterial({ color: 0xff0000, opacity: 0.9, transparent: true }); // Red for inverted up vector
    const secondProjectedMaterial = new THREE.MeshBasicMaterial({ color: 0x00ff00, opacity: 0.2, transparent: true }); // Green for second projection
    
    // Create forward cylinder mesh
    const forwardCylinder = new THREE.Mesh(geometry, forwardMaterial);
    forwardCylinder.rotation.x = Math.PI / 2;
    
    // Create up cylinder mesh
    const upCylinder = new THREE.Mesh(geometry, upMaterial);
    upCylinder.rotation.x = Math.PI / 2;
    
    // Create projected cylinder mesh
    const projectedCylinder = new THREE.Mesh(geometry, projectedMaterial);
    projectedCylinder.rotation.x = Math.PI / 2;
    projectedCylinder.visible = false; // Initially hidden

    // Create inverted up cylinder mesh
    const invertedUpCylinder = new THREE.Mesh(geometry, invertedUpMaterial);
    invertedUpCylinder.rotation.x = Math.PI / 2;
    invertedUpCylinder.visible = false; // Initially hidden

    // Create second projected cylinder mesh
    const secondProjectedCylinder = new THREE.Mesh(geometry, secondProjectedMaterial);
    secondProjectedCylinder.rotation.x = Math.PI / 2;
    secondProjectedCylinder.visible = false; // Initially hidden
    
    // Create a group to hold all cylinders
    const arrowGroup = new THREE.Group();
    arrowGroup.add(forwardCylinder);
    arrowGroup.add(upCylinder);
    arrowGroup.add(projectedCylinder);
    arrowGroup.add(invertedUpCylinder);
    arrowGroup.add(secondProjectedCylinder);
    
    // Add to scene
    scene.add(arrowGroup);
    
    // Store in Map with all cylinders and the color
    trackerArrows.set(deviceId, {
        forward: forwardCylinder,
        up: upCylinder,
        projected: projectedCylinder,
        invertedUp: invertedUpCylinder,
        secondProjected: secondProjectedCylinder,
        group: arrowGroup,
        color: color
    });
}

function getSortedTrackerArrows(trackerArrows) {
    return Array.from(trackerArrows.entries())
        .sort(([idA, _], [idB, __]) => {
            // Get device types and positions
            const isDeviceA = {
                glove: idA.toLowerCase().includes('glove'),
                right: isRightSide(idA),
                lower: isLowerArm(idA)
            };
            
            const isDeviceB = {
                glove: idB.toLowerCase().includes('glove'),
                right: isRightSide(idB),
                lower: isLowerArm(idB)
            };
            
            // Sort by side first (right before left)
            if (isDeviceA.right !== isDeviceB.right) {
                return isDeviceA.right ? -1 : 1;
            }
            
            // For same side, sort by position (glove, lower arm, upper arm)
            if (isDeviceA.glove !== isDeviceB.glove) {
                return isDeviceA.glove ? -1 : 1;
            }
            
            // Both are trackers, sort by arm position (lower before upper)
            if (!isDeviceA.glove && isDeviceA.lower !== isDeviceB.lower) {
                return isDeviceA.lower ? -1 : 1;
            }
            
            // Fallback to default sort (ID)
            return idA.localeCompare(idB);
        })
        .map(([id, arrow]) => ({id, ...arrow}));
}

// Helper function to determine if a device is on the right side
function isRightSide(deviceId) {
    if (deviceId.toLowerCase().includes('glove')) {
        // For gloves, check the stored hand information
        const gloveData = gloves.get(deviceId);
        return gloveData ? gloveData.isRightHand : false;
    } else {
        // For trackers, check the stored arm information
        const trackerData = trackers.get(deviceId);
        return trackerData ? trackerData.isRightArm : false;
    }
}

// Helper function to determine if a tracker is for the lower arm
function isLowerArm(deviceId) {
    if (deviceId.toLowerCase().includes('glove')) {
        return false; // Gloves are not arm trackers
    }
    
    const trackerData = trackers.get(deviceId);
    return trackerData ? trackerData.isLowerArm : false;
}

function updateTrackerArrow(deviceId, quaternion) {
    const arrow = trackerArrows.get(deviceId);
    if (!arrow) return;

    // Create sorted array of tracker arrows with glove first
    const sortedTrackerArrows = getSortedTrackerArrows(trackerArrows);

    const thisIndex = sortedTrackerArrows.findIndex(arrow => arrow.id === deviceId);

    // Store the quaternion in the arrow object
    arrow.quaternion = quaternion;
    
    // Use shorter arrows for glove devices for better visual separation
    const isGloveDevice = deviceId.toLowerCase().includes('glove');
    const rayLength = isGloveDevice ? 2.0 : 5.0;
    
    // Calculate forward direction with variable length based on device type
    const forwardTip = forwardRay(quaternion, rayLength);
    
    // Calculate up direction
    const upLength = 1.0; // Shorter length for up vector
    const upTip = upRay(quaternion, upLength);
    const downTip = downRay(quaternion, upLength);

    // Calculate orthogonal direction using cross product
    const orthogonalTip = {
        x: forwardTip.y * upTip.z - forwardTip.z * upTip.y,
        y: forwardTip.z * upTip.x - forwardTip.x * upTip.z,
        z: forwardTip.x * upTip.y - forwardTip.y * upTip.x
    };
    
    // Normalize the orthogonal vector
    const orthoLength = Math.sqrt(orthogonalTip.x * orthogonalTip.x + orthogonalTip.y * orthogonalTip.y + orthogonalTip.z * orthogonalTip.z);
    orthogonalTip.x /= orthoLength;
    orthogonalTip.y /= orthoLength;
    orthogonalTip.z /= orthoLength;
    
    // Use helper to draw cylinders between origin and tip points
    setCylinderBetweenPoints(
        arrow.forward,
        new THREE.Vector3(0, 0, 0),
        new THREE.Vector3(forwardTip.x, forwardTip.y, forwardTip.z)
    );

    setCylinderBetweenPoints(
        arrow.up,
        new THREE.Vector3(0, 0, 0),
        new THREE.Vector3(upTip.x, upTip.y, upTip.z)
    );

    arrow.upTip = { ...upTip }; // Store up tip for repositioning

    if (thisIndex === 0) {
        currentArmAngles.right.lowerArmRotation = calculateRollAroundForward(forwardTip, upTip);
        // updateArmValuesDisplay();  // Commented out for hand-only display
    } 

    // Calculate angles between this tracker and all other trackers
    if (sortedTrackerArrows.length > 1) {
        let angleInfo = [];
        
        for (let otherIndex = 0; otherIndex < sortedTrackerArrows.length; otherIndex++) {
            const otherArrow = sortedTrackerArrows[otherIndex];
            const otherId = otherArrow.id;

            // console.log(`this: ${deviceId}, other: ${otherId}, otherIndex: ${otherIndex}`);

            if (otherId !== deviceId) {
                const otherTip = {
                    x: otherArrow.forward.position.x * 2,
                    y: otherArrow.forward.position.y * 2,
                    z: otherArrow.forward.position.z * 2
                };

                // Calculate direct angle between forwardTip and otherTip
                const dotProduct = forwardTip.x * otherTip.x + forwardTip.y * otherTip.y + forwardTip.z * otherTip.z;
                const forwardLength = Math.sqrt(forwardTip.x * forwardTip.x + forwardTip.y * forwardTip.y + forwardTip.z * forwardTip.z);
                const otherLength = Math.sqrt(otherTip.x * otherTip.x + otherTip.y * otherTip.y + otherTip.z * otherTip.z);
                const cosAngle = dotProduct / (forwardLength * otherLength);
                const directAngle = Math.acos(Math.max(-1, Math.min(1, cosAngle))) * (180 / Math.PI);

                // Create a vector that combines both forward and up components from the OTHER tracker
                const otherForwardTip = {
                    x: otherArrow.forward.position.x * 2,
                    y: otherArrow.forward.position.y * 2,
                    z: otherArrow.forward.position.z * 2
                };
                const otherUpTip = {
                    x: -otherArrow.up.position.x * 2,
                    y: -otherArrow.up.position.y * 2,
                    z: -otherArrow.up.position.z * 2
                };

                const otherOrthogonalTip = {
                    x: otherForwardTip.y * otherUpTip.z - otherForwardTip.z * otherUpTip.y,
                    y: otherForwardTip.z * otherUpTip.x - otherForwardTip.x * otherUpTip.z,
                    z: otherForwardTip.x * otherUpTip.y - otherForwardTip.y * otherUpTip.x
                };

                // Create plane normal using forward vector and orthogonal vector
                const otherPlaneNormal = {
                    x: forwardTip.y * otherOrthogonalTip.z - forwardTip.z * otherOrthogonalTip.y,
                    y: forwardTip.z * otherOrthogonalTip.x - forwardTip.x * otherOrthogonalTip.z,
                    z: forwardTip.x * otherOrthogonalTip.y - forwardTip.y * otherOrthogonalTip.x
                };

                const combinedVector = {
                    x: otherForwardTip.x + otherUpTip.x,
                    y: otherForwardTip.y + otherUpTip.y,
                    z: otherForwardTip.z + otherUpTip.z
                };

                // Normalize the combined vector
                const combinedLength = Math.sqrt(
                    combinedVector.x * combinedVector.x +
                    combinedVector.y * combinedVector.y +
                    combinedVector.z * combinedVector.z
                );
                combinedVector.x /= combinedLength;
                combinedVector.y /= combinedLength;
                combinedVector.z /= combinedLength;

                // Scale the combined vector to match the forward length
                combinedVector.x *= forwardLength;
                combinedVector.y *= forwardLength;
                combinedVector.z *= forwardLength;

                // Project the combined vector onto the plane
                const projectedVector = projectVectorOntoPlane(combinedVector, otherPlaneNormal);

                // Update projected vector visualization
                const projectedLength = Math.sqrt(
                    projectedVector.x * projectedVector.x +
                    projectedVector.y * projectedVector.y +
                    projectedVector.z * projectedVector.z
                );

                const scale = forwardLength / projectedLength;
                const scaledProjectedVector = {
                    x: projectedVector.x * scale,
                    y: projectedVector.y * scale,
                    z: projectedVector.z * scale
                };

                // Only show projection for the first tracker
                if (otherIndex === 1 && thisIndex === 0) {
                    // Update the projected vector's material to use the source tracker's color
                    arrow.projected.material.color.setHex(otherArrow.color);
                    
                    // Position projected cylinder using helper
                    setCylinderBetweenPoints(
                        arrow.projected,
                        new THREE.Vector3(0, 0, 0),
                        new THREE.Vector3(
                            scaledProjectedVector.x,
                            scaledProjectedVector.y,
                            scaledProjectedVector.z
                        )
                    );
                    
                    // Make sure the projected vector is visible
                    arrow.projected.visible = true;
                    
                    // console.log("Projected vector visualization:", {
                    //     position: arrow.projected.position,
                    //     scale: arrow.projected.scale,
                    //     visible: arrow.projected.visible
                    // });
                }

                // Calculate wrist angles
                // Deviation: Signed angle between forward vector and projected vector
                const deviationAngle = calculateSignedAngle(
                    forwardTip,
                    scaledProjectedVector,
                    otherPlaneNormal
                );

                // Flexion/Extension: Signed angle between the projected vector and the other tracker's forward vector
                const flexionNormal = {
                    x: forwardTip.y * upTip.z - forwardTip.z * upTip.y,
                    y: forwardTip.z * upTip.x - forwardTip.x * upTip.z,
                    z: forwardTip.x * upTip.y - forwardTip.y * upTip.x
                };
                const flexionAngle = calculateSignedAngle(
                    otherTip,
                    scaledProjectedVector,
                    flexionNormal
                );

                // Only update wrist angles from the first two trackers
                if (thisIndex === 0 && otherIndex === 1) {
                    // const delta = deltaXY(arrow.quaternion, otherArrow.quaternion);
                    // wristFlexion = -delta.dX;
                    // wristDeviation = delta.dY;
                    currentArmAngles.right.wristFlexion = flexionAngle;
                    currentArmAngles.right.wristDeviation = -deviationAngle;
                    // updateArmValuesDisplay();  // Commented out for hand-only display

                } else if (thisIndex === 1 && otherIndex === 2) {

                    currentArmAngles.right.elbowFlexion = directAngle;
                    // updateArmValuesDisplay();  // Commented out for hand-only display

                } else if (thisIndex === 2) {
                    // Calculate shoulder angles based on third tracker's orientation
                    // Get the forward and up vectors from the third tracker
                    const shoulderForward = {
                        x: arrow.forward.position.x * 2,
                        y: arrow.forward.position.y * 2,
                        z: arrow.forward.position.z * 2
                    };
                    const shoulderUp = {
                        x: arrow.up.position.x * 2,
                        y: arrow.up.position.y * 2,
                        z: arrow.up.position.z * 2
                    };

                    // Normalize vectors
                    const normalize = (v) => {
                        const length = Math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                        return {
                            x: v.x / length,
                            y: v.y / length,
                            z: v.z / length
                        };
                    };
                    const forward = normalize(shoulderForward);
                    const up = normalize(shoulderUp);

                    // Calculate shoulder flexion (up/down movement)
                    // Project forward vector onto the vertical plane (XZ plane)
                    const forwardXZ = {
                        x: forward.x,
                        y: 0,
                        z: forward.z
                    };
                    const forwardXZLength = Math.sqrt(forwardXZ.x * forwardXZ.x + forwardXZ.z * forwardXZ.z);
                    if (forwardXZLength > 0) {
                        forwardXZ.x /= forwardXZLength;
                        forwardXZ.z /= forwardXZLength;
                    }
                    // Angle between forward vector and its projection on XZ plane
                    const flexionDot = forward.x * forwardXZ.x + forward.z * forwardXZ.z;
                    currentArmAngles.right.shoulderAbduction = Math.acos(Math.max(-1, Math.min(1, flexionDot))) * (180 / Math.PI);
                    // Make flexion negative when pointing down
                    if (forward.y < 0) currentArmAngles.right.shoulderAbduction = -currentArmAngles.right.shoulderAbduction;

                    // Calculate shoulder deviation (left/right movement)
                    // Project forward vector onto the horizontal plane (XY plane)
                    const forwardXY = {
                        x: forward.x,
                        y: forward.y,
                        z: 0
                    };
                    const forwardXYLength = Math.sqrt(forwardXY.x * forwardXY.x + forwardXY.y * forwardXY.y);
                    if (forwardXYLength > 0) {
                        forwardXY.x /= forwardXYLength;
                        forwardXY.y /= forwardXYLength;
                    }
                    // Angle between forward vector and its projection on XY plane
                    const deviationDot = forward.x * forwardXY.x + forward.y * forwardXY.y;
                    currentArmAngles.right.shoulderFlexion = Math.acos(Math.max(-1, Math.min(1, deviationDot))) * (180 / Math.PI);
                    // Make deviation negative when pointing left
                    if (forward.z > 0) currentArmAngles.right.shoulderFlexion = -currentArmAngles.right.shoulderFlexion;

                    // updateArmValuesDisplay();  // Commented out for hand-only display
                }

                // Only add angle info if this tracker is being projected onto
                if (otherArrow.projected.visible) {
                    angleInfo.push(`with ${otherId}: ${flexionAngle.toFixed(1)}°, ${deviationAngle.toFixed(1)}°`);
                }
            }
        }
    } else {
        // Hide projected vector if there are no other trackers
        arrow.projected.visible = false;
    }

    // In original updateTrackerArrow function, after forwardTip is calculated, store it
    arrow.forwardTip = { ...forwardTip }; // Save for chain positioning
    repositionTrackerArrows();
}

// Add function to calculate angle between two vectors
function calculateAngleBetweenVectors(v1, v2) {
    // Calculate dot product
    const dotProduct = v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
    
    // Calculate magnitudes
    const mag1 = Math.sqrt(v1.x * v1.x + v1.y * v1.y + v1.z * v1.z);
    const mag2 = Math.sqrt(v2.x * v2.x + v2.y * v2.y + v2.z * v2.z);
    
    // Calculate angle in radians
    const angleRad = Math.acos(dotProduct / (mag1 * mag2));
    
    // Convert to degrees
    return angleRad * (180 / Math.PI);
}

// Add helper function to project vector onto plane
function projectVectorOntoPlane(vector, planeNormal) {
    // console.log("Projecting vector:", vector);
    // console.log("Plane normal:", planeNormal);
    
    // Normalize the plane normal
    const normal = normalizeVector(planeNormal);
    // console.log("Normalized plane normal:", normal);
    
    // Calculate projection
    const dot = vector.x * normal.x + vector.y * normal.y + vector.z * normal.z;
    // console.log("Dot product:", dot);
    
    const result = {
        x: vector.x - dot * normal.x,
        y: vector.y - dot * normal.y,
        z: vector.z - dot * normal.z
    };
    // console.log("Projection result:", result);
    
    return result;
}

// Add helper function to normalize vector
function normalizeVector(v) {
    const length = Math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {
        x: v.x / length,
        y: v.y / length,
        z: v.z / length
    };
}

// Add helper function to calculate cross product
function crossProduct(v1, v2) {
    return {
        x: v1.y * v2.z - v1.z * v2.y,
        y: v1.z * v2.x - v1.x * v2.z,
        z: v1.x * v2.y - v1.y * v2.x
    };
}

// Add helper function to calculate signed angle between vectors
function calculateSignedAngle(v1, v2, normal) {
    // Normalize vectors
    const n1 = normalizeVector(v1);
    const n2 = normalizeVector(v2);
    
    // Calculate dot product
    const dot = n1.x * n2.x + n1.y * n2.y + n1.z * n2.z;
    
    // Calculate cross product
    const cross = crossProduct(n1, n2);
    
    // Calculate angle
    const angle = Math.acos(Math.max(-1, Math.min(1, dot))) * (180 / Math.PI);
    
    // Determine sign using the normal vector
    const sign = (cross.x * normal.x + cross.y * normal.y + cross.z * normal.z) >= 0 ? 1 : -1;
    
    return angle * sign;
}

// Add quaternion rotation functions
function rotateVector(q, v) {
    // Convert v to quaternion form (0, vx, vy, vz)
    const vx = v.x, vy = v.y, vz = v.z;
    // First multiply q * v
    const qw = q.w, qx = q.x, qy = q.y, qz = q.z;
    const iw = -qx*vx - qy*vy - qz*vz;
    const ix =  qw*vx + qy*vz - qz*vy;
    const iy =  qw*vy + qz*vx - qx*vz;
    const iz =  qw*vz + qx*vy - qy*vx;
    // Then multiply by q* (conjugate)
    return {
        x: ix*qw + iw*(-qx) + iy*(-qz) - iz*(-qy),
        y: iy*qw + iw*(-qy) + iz*(-qx) - ix*(-qz),
        z: iz*qw + iw*(-qz) + ix*(-qy) - iy*(-qx)
    };
}

function forwardRay(q, len = 1) {
    const fwd = { x: 0, y: 1, z: 0 };          // sensor's +Y
    const dir = rotateVector(q, fwd);
    return { x: dir.x * len, y: dir.z * len, z: -dir.y * len };
}

// Add function to calculate up vector from quaternion
function upRay(q, len = 1) {
    const up = { x: 0, y: 0, z: 1 };          // sensor's +Z is up
    const dir = rotateVector(q, up);
    return { x: dir.x * len, y: dir.z * len, z: -dir.y * len };
}

function downRay(q, len = 1) {
    const down = { x: 0, y: 0, z: -1 };          // sensor's -Z is down
    const dir = rotateVector(q, down);
    return { x: dir.x * len, y: dir.z * len, z: -dir.y * len };
}

// Add function to create direction vector from quaternion
function directionVector(q, len = 1) {
    // Forward direction (typically -Z axis)
    const forward = { x: 0, y: 0, z: -1 };
    return rotateVector(q, forward);
}

// Add global variables for wrist angles
// let wristFlexion = 0;
// let wristDeviation = 0;
// let lowerArmRotation = 0;
// let elbowFlexion = 0;
// let upperArmRotation = 0;
// let shoulderFlexion = 0;
// let shoulderAbduction = 0;

// New unified structure for arm angles per side
const currentArmAngles = {
    right: {
        wristFlexion: 0,
        wristDeviation: 0,
        lowerArmRotation: 0,
        elbowFlexion: 0,
        shoulderFlexion: 0,
        shoulderAbduction: 0
    },
    left: {
        wristFlexion: 0,
        wristDeviation: 0,
        lowerArmRotation: 0,
        elbowFlexion: 0,
        shoulderFlexion: 0,
        shoulderAbduction: 0
    }
};

// Function to update arm values display
function updateArmValuesDisplay() {
    document.getElementById('wrist-flexion').textContent = `${currentArmAngles.right.wristFlexion.toFixed(1)}°`;
    document.getElementById('wrist-deviation').textContent = `${currentArmAngles.right.wristDeviation.toFixed(1)}°`;
    document.getElementById('lower-arm-rotation').textContent = `${currentArmAngles.right.lowerArmRotation.toFixed(1)}°`;
    document.getElementById('upper-arm-rotation').textContent = `${currentArmAngles.right.shoulderFlexion.toFixed(1)}°`;
    document.getElementById('elbow-flexion').textContent = `${currentArmAngles.right.elbowFlexion.toFixed(1)}°`;
    document.getElementById('shoulder-flexion').textContent = `${currentArmAngles.right.shoulderFlexion.toFixed(1)}°`;
    document.getElementById('shoulder-deviation').textContent = `${currentArmAngles.right.shoulderAbduction.toFixed(1)}°`;

    // Get the first glove from the hands Map
    const rightGloveValue = Array.from(hands.values()).find(hand => hand.side === 'right' && hand.id && hand.id.toLowerCase().includes('eidon-glove')) ||
                             Array.from(hands.values()).find(hand => hand.side === 'right');
    const rightGloveId = rightGloveValue ? rightGloveValue.id : null;
    const rightHandModel = rightGloveId ? hands.get(rightGloveId) : null;

    if (rightHandModel && rightHandModel.arm && rightHandModel.arm.wrist && rightHandModel.arm.elbow) {
        // console.log('currentArmAngles:', currentArmAngles);
        // Convert degrees to radians
        const flexionRad = THREE.MathUtils.degToRad(-currentArmAngles.right.wristFlexion + 90);
        const deviationRad = THREE.MathUtils.degToRad(-currentArmAngles.right.wristDeviation);
        const rotationRad = THREE.MathUtils.degToRad(90 + currentArmAngles.right.lowerArmRotation);
        const elbowRad = THREE.MathUtils.degToRad(currentArmAngles.right.elbowFlexion);
        const shoulderFlexionRad = THREE.MathUtils.degToRad(currentArmAngles.right.shoulderFlexion);
        const shoulderAbductionRad = THREE.MathUtils.degToRad(currentArmAngles.right.shoulderAbduction + 90);
        
        // Apply rotations to the wrist joint
        const wristJoint = rightHandModel.arm.wrist;
        // Reset rotation first
        wristJoint.rotation.set(0, 0, 0);
        // Apply flexion (around X axis)
        wristJoint.rotateX(flexionRad);
        // Apply deviation (around Y axis)
        wristJoint.rotateY(deviationRad);
        // Apply rotation (around Z axis)
        // wristJoint.rotateZ(rotationRad);

        // Apply elbow flexion (around Y axis)
        const elbowJoint = rightHandModel.arm.elbow;
        elbowJoint.rotation.set(0, 0, 0);
        elbowJoint.rotateX(elbowRad);

        // Apply shoulder flexion (around X axis)
        const shoulderJoint = rightHandModel.arm.shoulder;
        shoulderJoint.rotation.set(0, 0, 0);
        shoulderJoint.rotateX(shoulderFlexionRad);
        // Apply shoulder deviation (around Z axis)
        // shoulderJoint.rotateZ(shoulderAbductionRad);
    }
}

/**
 * Quaternion → Euler (XYZ) in radians
 * Expects a unit quaternion: {x, y, z, w}
 * Returns [pitchX, yawY, rollZ]
 */
function quatToEulerXYZ(q) {
    const { x, y, z, w } = q;
  
    // Pitch (X-axis)
    const sinr = 2 * (w * x + y * z);
    const cosr = 1 - 2 * (x * x + y * y);
    const pitch = Math.atan2(sinr, cosr);
  
    // Yaw (Y-axis)
    const sinp = 2 * (w * y - z * x);
    const yaw = Math.abs(sinp) >= 1 ? Math.sign(sinp) * Math.PI / 2
                                    : Math.asin(sinp);
  
    // Roll (Z-axis)
    const siny = 2 * (w * z + x * y);
    const cosy = 1 - 2 * (y * y + z * z);
    const roll = Math.atan2(siny, cosy);
  
    return [pitch, yaw, roll];
  }
  
  /**
   * Hamilton product q1 * q2
   */
  function mulQuat(q1, q2) {
    return {
      w: q1.w*q2.w - q1.x*q2.x - q1.y*q2.y - q1.z*q2.z,
      x: q1.w*q2.x + q1.x*q2.w + q1.y*q2.z - q1.z*q2.y,
      y: q1.w*q2.y - q1.x*q2.z + q1.y*q2.w + q1.z*q2.x,
      z: q1.w*q2.z + q1.x*q2.y - q1.y*q2.x + q1.z*q2.w,
    };
  }
  
  /**
   * Conjugate (same as inverse for unit quats)
   */
  const conj = q => ({ w: q.w, x: -q.x, y: -q.y, z: -q.z });
  
  /**
   * Degrees of change about local X & Y axes from qA → qB
   * Returns { dX, dY } in **degrees**
   */
  function deltaXY(qA, qB) {
    // Relative rotation in A's local frame
    const qDelta = mulQuat(conj(qA), qB);
  
    // Convert to Euler; order XYZ so X & Y are what we care about
    const [pitchX, , rollY] = quatToEulerXYZ(qDelta);
  
    return {
      dX: pitchX * 180 / Math.PI,
      dY: rollY  * 180 / Math.PI,
    };
  }

  function calculateRollAroundForward(forward, up) {
    // Normalize the forward vector to create our Z axis
    const forwardLength = Math.sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    const zAxis = {
        x: forward.x / forwardLength,
        y: forward.y / forwardLength,
        z: forward.z / forwardLength
    };

    // Create a reference vector perpendicular to forward (we'll use the cross product with world up)
    const worldUp = { x: 0, y: 1, z: 0 };
    const xAxis = {
        x: worldUp.y * zAxis.z - worldUp.z * zAxis.y,
        y: worldUp.z * zAxis.x - worldUp.x * zAxis.z,
        z: worldUp.x * zAxis.y - worldUp.y * zAxis.x
    };
    
    // Normalize xAxis
    const xLength = Math.sqrt(xAxis.x * xAxis.x + xAxis.y * xAxis.y + xAxis.z * xAxis.z);
    xAxis.x /= xLength;
    xAxis.y /= xLength;
    xAxis.z /= xLength;

    // Create yAxis using cross product of zAxis and xAxis
    const yAxis = {
        x: zAxis.y * xAxis.z - zAxis.z * xAxis.y,
        y: zAxis.z * xAxis.x - zAxis.x * xAxis.z,
        z: zAxis.x * xAxis.y - zAxis.y * xAxis.x
    };

    // Project the up vector onto the XY plane
    const upDotX = up.x * xAxis.x + up.y * xAxis.y + up.z * xAxis.z;
    const upDotY = up.x * yAxis.x + up.y * yAxis.y + up.z * yAxis.z;
    
    // Calculate the angle in the XY plane
    const angle = Math.atan2(upDotY, upDotX) * (180 / Math.PI);
    
    return angle;
}

// Add dark mode toggle functionality
function addDarkModeToggle() {
    const toggle = document.createElement('button');
    toggle.id = 'dark-mode-toggle';
    toggle.textContent = '☀️ Light Mode';
    toggle.onclick = () => {
        const isDark = document.body.getAttribute('data-theme') === 'dark';
        document.body.setAttribute('data-theme', isDark ? 'light' : 'dark');
        toggle.textContent = isDark ? '🌙 Dark Mode' : '☀️ Light Mode';
        
        // Update Three.js scene background
        if (scene) {
            scene.background = new THREE.Color(isDark ? 0xf0f0f0 : 0x1a1a1a);
        }
        
        // Save preference
        localStorage.setItem('darkMode', !isDark);
    };
    
    document.body.appendChild(toggle);
    
    // Check for saved preference, default to dark mode if not set
    const savedDarkMode = localStorage.getItem('darkMode') !== 'false';
    if (savedDarkMode) {
        document.body.setAttribute('data-theme', 'dark');
        toggle.textContent = '☀️ Light Mode';
        if (scene) {
            scene.background = new THREE.Color(0x1a1a1a);
        }
    }
}

// Call this after Three.js initialization
addDarkModeToggle();

// Add global toggle for tracker arrow drawing mode
let chainMode = false; // false = all arrows from origin, true = chained vectors

// Function to reposition tracker arrows based on current mode
function repositionTrackerArrows() {
    if (!scene) return;
    if (!chainMode) {
        // Reset all arrow groups to origin
        for (const arrow of trackerArrows.values()) {
            arrow.group.position.set(0, 0, 0);
        }
        // displayDeviceGrouping(); // Update grouping display
        return;
    }

    // Build sorted array with body position ordering
    const sortedTrackerArrows = getSortedTrackerArrows(trackerArrows);
    
    // Group devices into sets of three (right set and left set)
    const rightSet = [];
    const leftSet = [];
    
    sortedTrackerArrows.forEach(arrow => {
        // Check if this is a right-side device
        const isRightSide = arrow.id.toLowerCase().includes('glove') 
            ? (gloves.get(arrow.id)?.isRightHand ?? false)
            : (trackers.get(arrow.id)?.isRightArm ?? false);
            
        if (isRightSide) {
            rightSet.push(arrow);
        } else {
            leftSet.push(arrow);
        }
    });

    // Position for right side devices (starts at right side of screen)
    const rightStartPosition = { x: 2, y: 5, z: 0 };
    positionChainedSet(rightSet, rightStartPosition);
    
    // Position for left side devices (starts at left side of screen)
    const leftStartPosition = { x: -2, y: 5, z: 0 };
    positionChainedSet(leftSet, leftStartPosition);
    
    // Update the grouping display
    // displayDeviceGrouping();
}

// Helper function to position a set of chained devices
function positionChainedSet(deviceSet, startPosition) {
    if (deviceSet.length === 0) return;
    
    // Reverse the device set to get anatomical order: upper arm → lower arm → glove
    // This will make the glove appear at the end of the chain
    const reversedSet = [...deviceSet].reverse();
    
    let cumulativeOffset = { ...startPosition };
    
    // Position each device in the set
    for (let i = 0; i < reversedSet.length; i++) {
        const arrow = reversedSet[i];
        
        // Set position for this arrow
        arrow.group.position.set(cumulativeOffset.x, cumulativeOffset.y, cumulativeOffset.z);
        
        // Add this arrow's forward tip to cumulative offset if we have it
        if (arrow.forwardTip) {
            cumulativeOffset.x += arrow.forwardTip.x;
            cumulativeOffset.y += arrow.forwardTip.y;
            cumulativeOffset.z += arrow.forwardTip.z;
        }
    }
}

// Add vector mode toggle UI
function addVectorModeToggle() {
    // Prevent duplicate button
    let toggle = document.getElementById('vector-mode-toggle');
    if (toggle) {
        toggle.onclick = () => {
            chainMode = !chainMode;
            updateChainModeButtonText(toggle);
            repositionTrackerArrows();
            // Explicitly call displayDeviceGrouping to update the grouping info
            // displayDeviceGrouping();
        };
        return;
    }
    toggle = document.createElement('button');
    toggle.id = 'vector-mode-toggle';
    toggle.textContent = 'Chain Mode: Off';
    toggle.onclick = () => {
        chainMode = !chainMode;
        updateChainModeButtonText(toggle);
        repositionTrackerArrows();
        // Explicitly call displayDeviceGrouping to update the grouping info
        // displayDeviceGrouping();
    };

    // Find or create the view-controls container (same logic as gamepad button)
    let viewControls = document.querySelector('.view-controls');
    if (!viewControls) {
        viewControls = document.createElement('div');
        viewControls.className = 'view-controls';
        const controlsContainer = document.querySelector('.controls') || document.body;
        controlsContainer.appendChild(viewControls);
    }

    viewControls.appendChild(toggle);
    updateChainModeButtonText(toggle);
    
    // Initialize device grouping display
    // displayDeviceGrouping();
}

// Helper function to update chain mode button text
function updateChainModeButtonText(buttonElement) {
    if (!buttonElement) return;
    
    if (chainMode) {
        buttonElement.textContent = 'Chain Mode: On (Anatomical Order)';
        buttonElement.title = 'Devices are grouped by right/left sides and chained in anatomical order: Upper Arm → Lower Arm → Glove';
    } else {
        buttonElement.textContent = 'Chain Mode: Off';
        buttonElement.title = 'All devices are positioned at the origin';
    }
}

// Call this after dark mode toggle setup
// addVectorModeToggle();

// Utility: create a cylinder that can be stretched between two points
function createThickLineCylinder(radius = 0.1, color = 0xffffff, radialSegments = 8) {
    const geometry = new THREE.CylinderGeometry(radius, radius, 1, radialSegments);
    const material = new THREE.MeshBasicMaterial({ color });
    const mesh = new THREE.Mesh(geometry, material);
    return mesh;
}

// Utility: position & orient a cylinder so it spans start → end
function setCylinderBetweenPoints(cylinder, start, end) {
    const dir = new THREE.Vector3().subVectors(end, start);
    const len = dir.length();
    if (len === 0) return;

    // Scale to correct length (original height is 1)
    cylinder.scale.set(1, len, 1);

    // Move to midpoint
    const mid = new THREE.Vector3().addVectors(start, end).multiplyScalar(0.5);
    cylinder.position.copy(mid);

    // Orient so +Y of cylinder matches dir
    const quat = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.clone().normalize());
    cylinder.setRotationFromQuaternion(quat);
}

// NEW: global calibration button and function definitions
function addGlobalCalibrationButton() {
    const controlPanel = document.querySelector('.controls');
    if (!controlPanel) return;

    // Avoid duplicates
    let calibrateAllBtn = document.getElementById('calibrate-all-btn');
    if (calibrateAllBtn) {
        calibrateAllBtn.onclick = () => calibrateDevice();
        return;
    }

    calibrateAllBtn = document.createElement('button');
    calibrateAllBtn.id = 'calibrate-all-btn';
    calibrateAllBtn.textContent = 'Calibrate All';
    calibrateAllBtn.onclick = () => calibrateDevice(); // no arg => all devices
    controlPanel.appendChild(calibrateAllBtn);
}

// Send calibration command (reportId 1, value 1) to one or all devices
async function calibrateDevice(deviceId = null) {
    const REPORT_ID = 1;
    const payload = new Uint8Array([1]);

    // Helper to send to a specific HIDDevice instance
    const sendTo = async (id, device) => {
        try {
            await device.sendReport(REPORT_ID, payload);
            addLogMessage(`Calibration command sent to ${device.productName || id}`);
        } catch (err) {
            console.error('Calibration error for device', id, err);
            addLogMessage(`Calibration error for ${device.productName || id}: ${err.message}`);
        }
    };

    if (deviceId) {
        const device = hidDevices.get(deviceId);
        if (device) {
            await sendTo(deviceId, device);
        }
    } else {
        for (const [id, device] of hidDevices) {
            await sendTo(id, device);
        }
    }
}

// Helper: calibrate all trackers on the same arm as the specified glove
function calibrateTrackersOnSameArm(gloveDeviceId) {
    const gloveData = gloves.get(gloveDeviceId);
    if (!gloveData) return;

    const targetIsRight = gloveData.isRightHand;

    for (const [trackerId, trackerInfo] of trackers) {
        // Only calibrate trackers that have side information and match the glove's side
        if (trackerInfo && typeof trackerInfo.isRightArm === 'boolean' && trackerInfo.isRightArm === targetIsRight) {
            console.log('trackerId:', trackerId);
            calibrateDevice(trackerId);
        }
    }

    const sideLabel = targetIsRight ? 'Right' : 'Left';
    addLogMessage(`Glove ${gloveDeviceId} requested calibration for ${sideLabel} arm trackers.`);
}

// Call global calibration button setup after vector mode toggle
// addGlobalCalibrationButton();

// Attach a hidden colour picker to the dot, send result to device & update arrow
function attachColorPicker(dotEl, deviceId) {
    const picker = document.createElement('input');
    picker.type = 'color';
    picker.style.display = 'none';
    document.body.appendChild(picker);

    // Get the current color from the dot's background color
    const getCurrentColor = () => {
        const bgColor = window.getComputedStyle(dotEl).backgroundColor;
        // Convert RGB format to hex
        if (bgColor.startsWith('rgb')) {
            const rgb = bgColor.match(/\d+/g);
            if (rgb && rgb.length >= 3) {
                const hexColor = '#' + 
                    parseInt(rgb[0]).toString(16).padStart(2, '0') +
                    parseInt(rgb[1]).toString(16).padStart(2, '0') +
                    parseInt(rgb[2]).toString(16).padStart(2, '0');
                return hexColor;
            }
        }
        // If color is already in hex format or can't be determined, use white
        return '#ffffff';
    };

    dotEl.style.cursor = 'pointer';
    dotEl.addEventListener('click', (event) => {
        // Set initial color value
        picker.value = getCurrentColor();
        
        // Position the picker near the clicked dot
        picker.style.position = 'absolute';
        picker.style.display = 'block';
        
        // Calculate position
        const rect = dotEl.getBoundingClientRect();
        const scrollTop = window.scrollY || document.documentElement.scrollTop;
        const scrollLeft = window.scrollX || document.documentElement.scrollLeft;
        
        // Position above the dot with some offset
        const top = rect.top + scrollTop - 40;
        const left = rect.left + scrollLeft - 40;
        
        // Ensure picker stays within viewport
        const viewportHeight = window.innerHeight;
        const viewportWidth = window.innerWidth;
        const pickerHeight = 40; // Approximate height of color picker
        const pickerWidth = 40;  // Approximate width of color picker
        
        const finalTop = Math.max(0, Math.min(top, viewportHeight - pickerHeight));
        const finalLeft = Math.max(0, Math.min(left, viewportWidth - pickerWidth));
        
        picker.style.top = `${finalTop}px`;
        picker.style.left = `${finalLeft}px`;
        
        // Trigger click after positioning
        setTimeout(() => picker.click(), 0);
        
        // Add one-time event to hide the picker element when clicking away
        document.addEventListener('click', function hideOnClickAway(e) {
            if (e.target !== picker) {
                picker.style.display = 'none';
                document.removeEventListener('click', hideOnClickAway);
            }
        });
        
        // Prevent the click from propagating to the hideOnClickAway handler
        event.stopPropagation();
    });

    picker.addEventListener('input', async () => {
        const hex = picker.value.substring(1); // "RRGGBB"
        const colorInt = parseInt(hex, 16);
        // Update UI dot
        dotEl.style.backgroundColor = '#' + hex;

        // Update arrow materials
        const arrow = trackerArrows.get(deviceId);
        if (arrow) {
            [arrow.forward.material, arrow.up.material].forEach(m=>m.color.setHex(colorInt));
            arrow.color = colorInt;
        }

        // Send to device via feature report
        const device = hidDevices.get(deviceId);
        if (device) {
            const r = (colorInt >> 16) & 0xFF;
            const g = (colorInt >> 8) & 0xFF;
            const b = colorInt & 0xFF;
            try {
                await device.sendFeatureReport(1, new Uint8Array([r, g, b]));
            } catch(e) {
                console.warn('Failed to send colour feature', e);
            }
        }
        
        // Save color to cache
        setCachedColor(deviceId, colorInt);
    });
}

// --- Persistent colour cache (deviceId → int) ---
function getStoredDeviceColors() {
    try {
        return JSON.parse(localStorage.getItem('deviceColors') || '{}');
    } catch (e) {
        return {};
    }
}
function getCachedColor(deviceId) {
    const map = getStoredDeviceColors();
    return Object.prototype.hasOwnProperty.call(map, deviceId) ? map[deviceId] : null;
}
function setCachedColor(deviceId, colorInt) {
    const map = getStoredDeviceColors();
    map[deviceId] = colorInt;
    localStorage.setItem('deviceColors', JSON.stringify(map));
}

// Add a helper function to update the glove hand display
function updateGloveHandPosition(deviceId, isRightHand) {
    const gloveElement = document.getElementById(`glove-${deviceId}`);
    if (!gloveElement) return;
    
    // Find or create the hand position span
    let handPositionSpan = document.getElementById(`hand-position-${deviceId}`);
    if (!handPositionSpan) {
        const gloveControls = gloveElement.querySelector('.glove-controls');
        if (gloveControls) {
            // Create position info with appropriate emoji and text
            handPositionSpan = document.createElement('span');
            handPositionSpan.id = `hand-position-${deviceId}`;
            handPositionSpan.className = 'device-hand-position';
            gloveControls.append(handPositionSpan);
        }
    }
    
    if (handPositionSpan) {
        // Set emoji based on left/right handhand
        const handEmoji = isRightHand ? '🤚' : '✋';
        const handText = isRightHand ? 'Right Hand' : 'Left Hand';
        
        // Update text content with emoji
        handPositionSpan.innerHTML = `${handEmoji} ${handText}`;
    }

    // Update the hand model side property if it exists
    const handModel = hands.get(deviceId);
    if (handModel) {
        handModel.side = isRightHand ? 'right' : 'left';
        // Ensure the hand appears on the correct side of the avatar
        const targetX = handModel.side === 'right' ? 4 : -4;
        if (handModel.arm && handModel.arm.shoulder) {
            handModel.arm.shoulder.position.x = targetX;
        }
    }

    // If this is a real glove, remove any placeholder hand on the same side
    if (!deviceId.startsWith('default')) {
        const placeholderId = isRightHand ? 'default-right' : 'default-left';
        if (hands.has(placeholderId)) {
            const placeholder = hands.get(placeholderId);
            if (placeholder && placeholder.arm && placeholder.arm.shoulder) {
                scene.remove(placeholder.arm.shoulder);
            }
            hands.delete(placeholderId);
        }
    }
}

// Add a function to display device grouping information
function displayDeviceGrouping() {
    // Check if the sidebar exists
    if (!jointsContainer) return;

    // Check if grouping info already exists in sidebar
    let groupingInfo = document.getElementById('device-grouping-info');

    if (!groupingInfo) {
        // Create info container in the sidebar
        groupingInfo = document.createElement('div');
        groupingInfo.id = 'device-grouping-info';
        groupingInfo.className = 'tracker-info device-grouping';
        // Minimal extra styling so it blends with tracker-info look
        groupingInfo.style.cssText = `margin-top: 10px; font-size: 12px;`;
        
        // Insert at the top of the sidebar
        if (jointsContainer.firstChild) {
            jointsContainer.insertBefore(groupingInfo, jointsContainer.firstChild);
        } else {
            jointsContainer.appendChild(groupingInfo);
        }
        
        // Add a style for dark/light mode compatibility
        const style = document.createElement('style');
        style.textContent = `
            [data-theme="light"] .device-grouping-sidebar {
                background: rgba(240, 240, 240, 0.8);
                color: black;
            }
        `;
        document.head.appendChild(style);
    }

    // Always show the grouping info box
    groupingInfo.style.display = 'block';

    // Get sorted devices
    const sortedTrackerArrows = getSortedTrackerArrows(trackerArrows);
    
    // Group devices by side and position
    const rightGlove = [];
    const rightLower = [];
    const rightUpper = [];
    const leftGlove = [];
    const leftLower = [];
    const leftUpper = [];

    sortedTrackerArrows.forEach(arrow => {
        const isGlove = arrow.id.toLowerCase().includes('glove');
        const isRight = isRightSide(arrow.id);
        const isLower = isLowerArm(arrow.id);
        
        if (isRight) {
            if (isGlove) rightGlove.push(arrow.id);
            else if (isLower) rightLower.push(arrow.id);
            else rightUpper.push(arrow.id);
        } else {
            if (isGlove) leftGlove.push(arrow.id);
            else if (isLower) leftLower.push(arrow.id);
            else leftUpper.push(arrow.id);
        }
    });

    // Format the display text
    let html = '<div class="grouping-header">Device Grouping (Chain Order)</div>';
    
    html += '<div class="side-group"><b>RIGHT ARM:</b></div>';
    // Show in reverse order (matching the chain direction)
    html += formatDeviceList(rightUpper);
    html += ' → <br>';
    html += formatDeviceList(rightLower);
    html += ' → <br>';
    html += formatDeviceList(rightGlove);
    html += '</div>';
    
    html += '<div class="side-group"><b>LEFT ARM:</b></div>';
    // Show in reverse order (matching the chain direction)
    html += formatDeviceList(leftUpper);
    html += ' → <br>';
    html += formatDeviceList(leftLower);
    html += ' → <br>';
    html += formatDeviceList(leftGlove);
    html += '</div>';

    // Add chain mode status
    html += '<div class="chain-mode-status">';
    html += chainMode ? 
        '<span style="color: #4CAF50;">Chain Mode: ON</span>' : 
        '<span style="color: #F44336;">Chain Mode: OFF</span>';
    html += '</div>';

    groupingInfo.innerHTML = html;
}

// Helper function to format device ID list
function formatDeviceList(deviceIds) {
    if (deviceIds.length === 0) return '<span style="color: #777777">None</span>';
    
    return deviceIds.map(id => {
        // Get device from hidDevices map
        const device = hidDevices.get(id);
        const name = device ? device.productName : id.split('-').pop();
        
        // Get color from trackerArrow
        const arrow = trackerArrows.get(id);
        const color = arrow ? arrow.color : 0xFFFFFF;
        const colorHex = color.toString(16).padStart(6, '0');
        
        return `<span style="color: #${colorHex}">■</span> ${name}`;
    }).join(', ');
}

// Function to initialize placeholder hands when no gloves are connected
function initializeDefaultHands() {
    // Right hand placeholder
    if (!hands.has('default-right')) {
        const rightHand = createHandModel('default-right');
        if (rightHand) {
            rightHand.side = 'right';
            // Place on positive X side
            rightHand.arm.shoulder.position.set(4, 15, 0);
        }
    }
    // Left hand placeholder
    if (!hands.has('default-left')) {
        const leftHand = createHandModel('default-left');
        if (leftHand) {
            leftHand.side = 'left';
            // Place on negative X side
            leftHand.arm.shoulder.position.set(-4, 15, 0);
        }
    }
}

// Add function to create IMU coordinate system controls
function addIMUCoordinateControls() {
    // Check if controls already exist
    if (document.getElementById('imu-coordinate-controls')) {
        return;
    }
    
    const controlPanel = document.querySelector('.controls');
    if (!controlPanel) return;
    
    // Create controls container
    const imuControls = document.createElement('div');
    imuControls.id = 'imu-coordinate-controls';
    imuControls.style.cssText = `
        margin-top: 10px; 
        padding: 10px; 
        border: 1px solid #ccc; 
        border-radius: 5px;
        background: rgba(255,255,255,0.1);
        font-size: 12px;
    `;
    
    // Add title
    const title = document.createElement('div');
    title.textContent = '🧭 IMU Coordinate Mapping';
    title.style.cssText = 'font-weight: bold; margin-bottom: 5px;';
    imuControls.appendChild(title);
    
    // Create mapping dropdown
    const mappingContainer = document.createElement('div');
    mappingContainer.style.cssText = 'margin-bottom: 5px;';
    
    const mappingLabel = document.createElement('label');
    mappingLabel.textContent = 'Quaternion Mapping: ';
    mappingLabel.style.cssText = 'margin-right: 5px;';
    
    const mappingSelect = document.createElement('select');
    mappingSelect.style.cssText = 'margin-right: 10px;';
    
    // Add mapping options
    const mappingOptions = [
        { value: 'original', text: 'Original (no transform)' },
        { value: 'rotate_x_90', text: 'Rotate 90° around X axis' },
        { value: 'rotate_x_180', text: 'Rotate 180° around X axis' },
        { value: 'rotate_x_270', text: 'Rotate 270° around X axis' },
        { value: 'rotate_y_90', text: 'Rotate 90° around Y axis' },
        { value: 'rotate_y_180', text: 'Rotate 180° around Y axis' },
        { value: 'rotate_y_270', text: 'Rotate 270° around Y axis' },
        { value: 'rotate_z_90', text: 'Rotate 90° around Z axis' },
        { value: 'rotate_z_180', text: 'Rotate 180° around Z axis' },
        { value: 'rotate_z_270', text: 'Rotate 270° around Z axis' },
        { value: 'x_z_y_w', text: 'Legacy: Swap Y↔Z axes' },
        { value: 'x_neg_z_y_w', text: 'Legacy: Swap Y↔Z + flip' },
        { value: 'neg_x_y_z_w', text: 'Legacy: Flip X axis' },
        { value: 'y_x_z_w', text: 'Legacy: Swap X↔Y axes' },
        { value: 'z_y_x_w', text: 'Legacy: Z→X transform' },
        { value: 'y_z_x_w', text: 'Legacy: Complex transform' },
        { value: 'neg_z_y_x_w', text: 'Legacy: Roll forward' },
        { value: 'x_y_neg_z_w', text: 'Legacy: Pitch forward' },
        { value: 'z_x_y_w', text: 'Legacy: Complex Y+Z transform' },
        { value: 'neg_y_x_z_w', text: 'Legacy: Complex Z+X transform' }
    ];
    
    mappingOptions.forEach(option => {
        const optionElement = document.createElement('option');
        optionElement.value = option.value;
        optionElement.textContent = option.text;
        if (option.value === IMU_COORDINATE_CONFIG.mapping) {
            optionElement.selected = true;
        }
        mappingSelect.appendChild(optionElement);
    });
    
    mappingSelect.addEventListener('change', (e) => {
        IMU_COORDINATE_CONFIG.mapping = e.target.value;
        // Immediately update all hand models
        for (const deviceId of hands.keys()) {
            updateHandModel(deviceId);
        }
    });
    
    mappingContainer.appendChild(mappingLabel);
    mappingContainer.appendChild(mappingSelect);
    imuControls.appendChild(mappingContainer);
    
    // Create rotation adjustment controls
    const rotationContainer = document.createElement('div');
    rotationContainer.style.cssText = 'display: flex; gap: 10px; align-items: center;';
    
    ['x', 'y', 'z'].forEach(axis => {
        const axisContainer = document.createElement('div');
        axisContainer.style.cssText = 'display: flex; flex-direction: column; align-items: center;';
        
        const label = document.createElement('label');
        label.textContent = `${axis.toUpperCase()}°:`;
        label.style.cssText = 'font-size: 10px; margin-bottom: 2px;';
        
        const input = document.createElement('input');
        input.type = 'range';
        input.min = '-180';
        input.max = '180';
        input.step = '5';
        input.value = (IMU_COORDINATE_CONFIG.additionalRotation[axis] * 180 / Math.PI).toString();
        input.style.cssText = 'width: 60px;';
        
        const valueSpan = document.createElement('span');
        valueSpan.textContent = input.value + '°';
        valueSpan.style.cssText = 'font-size: 10px;';
        
        input.addEventListener('input', (e) => {
            const degrees = parseFloat(e.target.value);
            valueSpan.textContent = degrees + '°';
            IMU_COORDINATE_CONFIG.additionalRotation[axis] = degrees * Math.PI / 180;
            
            // Immediately update all hand models
            for (const deviceId of hands.keys()) {
                updateHandModel(deviceId);
            }
        });
        
        axisContainer.appendChild(label);
        axisContainer.appendChild(input);
        axisContainer.appendChild(valueSpan);
        rotationContainer.appendChild(axisContainer);
    });
    
    imuControls.appendChild(rotationContainer);
    
    // Add reset button
    const resetButton = document.createElement('button');
    resetButton.textContent = 'Reset to Neutral';
    resetButton.style.cssText = 'margin-top: 5px; padding: 2px 8px; font-size: 10px;';
    resetButton.onclick = () => {
        // Reset to original mapping
        IMU_COORDINATE_CONFIG.mapping = 'original';
        mappingSelect.value = 'original';
        
        // Reset all rotations to zero
        IMU_COORDINATE_CONFIG.additionalRotation.x = 0;
        IMU_COORDINATE_CONFIG.additionalRotation.y = 0;
        IMU_COORDINATE_CONFIG.additionalRotation.z = 0;
        
        // Update slider values
        const sliders = rotationContainer.querySelectorAll('input[type="range"]');
        const valueSpans = rotationContainer.querySelectorAll('span');
        sliders.forEach((slider, index) => {
            slider.value = '0';
            if (valueSpans[index]) valueSpans[index].textContent = '0°';
        });
        
        // Update all hand models
        for (const deviceId of hands.keys()) {
            updateHandModel(deviceId);
        }
    };
    imuControls.appendChild(resetButton);
    
    controlPanel.appendChild(imuControls);
}

// Call this after Three.js initialization
addDarkModeToggle();

// Add recording controls
// addRecordingControls();

// Add IMU coordinate system controls
// addIMUCoordinateControls();
