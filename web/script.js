// Global variables for serial communication
let port;
let reader;
let keepReading = true;
let decoder = new TextDecoder();
let inputBuffer = '';
const MAX_JOINTS = 16;

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
    const handCount = getHandCount();
    const handModel = {
        arm: {
            shoulder: null,
            upperArm: null,
            elbow: null,
            forearm: null,
            wrist: null
        },
        palm: null,
        fingers: []
    };

    // Create materials
    const palmMaterial = new THREE.MeshPhongMaterial({ color: 0xf5c396 });
    const fingerMaterial = new THREE.MeshPhongMaterial({ color: 0xf5c396 });
    const jointMaterial = new THREE.MeshPhongMaterial({ color: 0xe3a977 });
    const armMaterial = new THREE.MeshPhongMaterial({ color: 0xf5c396 });

    // Create arm components
    // Shoulder joint (sphere)
    const shoulderGeometry = new THREE.SphereGeometry(1.5, 16, 16);
    handModel.arm.shoulder = new THREE.Mesh(shoulderGeometry, jointMaterial);
    handModel.arm.shoulder.position.set(handCount * 8 + 4, 15, 0); // Position shoulder higher up and space them out

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
    
    // Update arm position first
    updateArmPosition(deviceId);

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
    // const euler = gloveData.euler;
    // const roll = Math.PI - (euler.roll);
    // const pitch = Math.PI - (euler.pitch + Math.PI);
    // const yaw = euler.yaw + Math.PI;

    // handModel.palm.rotation.x = pitch;
    // handModel.palm.rotation.y = yaw;
    // handModel.palm.rotation.z = roll;
    
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

        // Create display for the device
        if (device.productName.toLowerCase().includes('tracker')) {
            console.log('Creating tracker display for:', deviceId);
            addTrackerDisplay(deviceId);
        } else {
            console.log('Creating glove display for:', deviceId);
            addGloveDisplay(deviceId);
            createHandModel(deviceId);
        }
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
    
    const pitch = Math.asin(2.0 * (w * y - z * x));
    
    const roll = Math.atan2(2.0 * (w * x + y * z),
                           1.0 - 2.0 * (x * x + y * y));

    // Convert to degrees and swap pitch and roll
    return {
        yaw: yaw * RAD_TO_DEG,
        roll: pitch * RAD_TO_DEG,  // Use pitch value for roll
        pitch: roll * RAD_TO_DEG   // Use roll value for pitch
    };
}

// Modify handleHIDInput to use the new multi-hand system
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
        const quaternionX = ((data.getUint8(19) | (data.getUint8(20) << 8)) - 32768) / 32767.5;
        const quaternionY = ((data.getUint8(21) | (data.getUint8(22) << 8)) - 32768) / 32767.5;
        const quaternionZ = ((data.getUint8(23) | (data.getUint8(24) << 8)) - 32768) / 32767.5;
        const quaternionW = ((data.getUint8(25) | (data.getUint8(26) << 8)) - 32768) / 32767.5;

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

// Modify addTrackerDisplay function
function addTrackerDisplay(deviceId) {
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
    const color = getColorFromDeviceName(deviceId);
    const colorHex = color ? '#' + color.toString(16).padStart(6, '0') : '#ffffff';
    
    trackerElement.innerHTML = `
        <div class="tracker-header">
            <div class="tracker-name">
                <span class="device-dot" style="background-color: ${colorHex}"></span>
                ${deviceName}
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
            <div class="quaternion-values">
                <div>X: <span id="tracker-quat-x-${deviceId}">0.000</span></div>
                <div>Y: <span id="tracker-quat-y-${deviceId}">0.000</span></div>
                <div>Z: <span id="tracker-quat-z-${deviceId}">0.000</span></div>
                <div>W: <span id="tracker-quat-w-${deviceId}">0.000</span></div>
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
    
    // Add to trackers Map with quaternion values
    trackers.set(deviceId, {
        quaternion: { x: 0, y: 0, z: 0, w: 1 },
        euler: { roll: 0, pitch: 0, yaw: 0 }
    });

    // Create tracker arrow in Three.js scene
    createTrackerArrow(deviceId);
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
function addGloveDisplay(deviceId) {
    const gloveId = deviceId.split('-').pop(); // Get unique part of device ID
    const gloveElement = document.createElement('div');
    gloveElement.className = 'glove-info';
    gloveElement.id = `glove-${deviceId}`;
    
    // Get device name from hidDevices
    const device = hidDevices.get(deviceId);
    const deviceName = device ? device.productName : 'Glove';
    
    // Get color for the dot
    const color = getColorFromDeviceName(deviceId);
    const colorHex = color ? '#' + color.toString(16).padStart(6, '0') : '#ffffff';
    
    // Create glove header
    const header = document.createElement('div');
    header.className = 'glove-header';
    header.innerHTML = `
        <div class="glove-name">
            <span class="device-dot" style="background-color: ${colorHex}"></span>
            ${deviceName}
        </div>
        <div class="glove-details">
            <span class="device-id">${deviceId}</span>
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
        <div class="joint-name">Orientation</div>
        <div class="quaternion-values">
            <div>X: <span id="quat-x-${deviceId}">0.000</span></div>
            <div>Y: <span id="quat-y-${deviceId}">0.000</span></div>
            <div>Z: <span id="quat-z-${deviceId}">0.000</span></div>
            <div>W: <span id="quat-w-${deviceId}">0.000</span></div>
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
        `;
        // <label class="invert-toggle">
        //     <input type="checkbox" id="invert-${deviceId}-${i}" ${fingerJointMap[i]?.inverted ? 'checked' : ''}>
        //     Invert Values
        // </label>
        glovejointsContainer.appendChild(jointElement);
    }

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

    // Create tracker arrow for the glove
    createTrackerArrow(deviceId);
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
function updateArmPosition(deviceId) {
    const handModel = hands.get(deviceId);
    if (!handModel) return;

    const gloveData = gloves.get(deviceId);
    const wristTracker = trackers.get(`0-0-Eidon-Tracker-1`);

    if (!gloveData || !wristTracker) return;

    // Get quaternion values from wrist tracker and hand
    const wristQuat = wristTracker.quaternion;
    const handQuat = gloveData.quaternion;

    // Set fixed arm positions
    handModel.arm.elbow.rotation.y = THREE.MathUtils.degToRad(0);
    handModel.arm.shoulder.rotation.x = THREE.MathUtils.degToRad(90);
    handModel.arm.shoulder.rotation.y = THREE.MathUtils.degToRad(180);
    
    // Create THREE.Quaternion objects
    const qWrist = new THREE.Quaternion(
        wristQuat.x,
        wristQuat.y,
        wristQuat.z,
        wristQuat.w
    );
    
    const qHand = new THREE.Quaternion(
        handQuat.x,
        handQuat.y,
        handQuat.z,
        handQuat.w
    );
    
    // Extract forearm roll from wrist tracker
    // This is the rotation around the forearm's long axis
    const forearmQuaternion = qWrist.clone();
    const forearmRoll = Math.atan2(2 * (forearmQuaternion.w * forearmQuaternion.z + forearmQuaternion.x * forearmQuaternion.y),
                                   1 - 2 * (forearmQuaternion.y * forearmQuaternion.y + forearmQuaternion.z * forearmQuaternion.z)) * (180 / Math.PI);
    
    // Apply forearm roll to the forearm
    handModel.arm.forearm.rotation.y = THREE.MathUtils.degToRad(-forearmRoll + 45);
    
    // Compute wrist flexion/extension angle
    const wristAngle = computeWristAngle(qWrist, qHand);
    
    // Apply the wrist angle to the wrist's x rotation
    handModel.arm.wrist.rotation.x = THREE.MathUtils.degToRad(-wristAngle + 90);
    
    // Compute radial/ulnar deviation angle (but don't apply it yet)
    const deviationAngle = computeWristDeviation(qWrist, qHand);
    
    // Reset other wrist rotations
    handModel.arm.wrist.rotation.y = 0;
    handModel.arm.wrist.rotation.z = 0;
    
    // Log the values for debugging
    console.log(`Forearm Roll: ${forearmRoll.toFixed(2)}°, Wrist Flexion: ${wristAngle.toFixed(2)}°, Radial/Ulnar Deviation: ${deviationAngle.toFixed(2)}°`);
}

function updateGloveDisplay(deviceId, data) {
    const gloveData = gloves.get(deviceId);
    if (!gloveData) return;

    // Update joint values
    for (let i = 0; i < MAX_JOINTS; i++) {
        const value = data.jointValues[i];
        const inverted = gloveData.jointInversions[i];
        const displayValue = inverted ? 1 - value : value;
        
        // Update joint value display
        const valueElement = document.getElementById(`joint-value-${deviceId}-${i}`);
        if (valueElement) {
            valueElement.textContent = `Value: ${displayValue.toFixed(3)}`;
        }
        
        // Update joint bar
        const barElement = document.getElementById(`joint-bar-${deviceId}-${i}`);
        if (barElement) {
            barElement.style.width = `${displayValue * 100}%`;
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
        return 0x61c680; // Use green color for gloves
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

function createTrackerArrow(deviceId) {
    // Create cylinder geometry for the forward arrow shaft
    const radius = 0.1; // Thickness of the arrow
    const height = 1;   // Initial height (will be scaled)
    const geometry = new THREE.CylinderGeometry(radius, radius, height, 8);
    
    // Try to get color from device name - check the entire device ID
    let color = getColorFromDeviceName(deviceId);
    
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
        .sort(([a], [b]) => {
            // Glove always first
            if (a.includes('eidon-glove')) return -1;
            if (b.includes('eidon-glove')) return 1;
            
            // White tracker second
            if (a.includes('eidon-tracker-white')) return -1;
            if (b.includes('eidon-tracker-white')) return 1;
            
            // Orange tracker third
            if (a.includes('eidon-tracker-orange')) return -1;
            if (b.includes('eidon-tracker-orange')) return 1;
            
            // Green tracker fourth
            if (a.includes('eidon-tracker-green')) return -1;
            if (b.includes('eidon-tracker-green')) return 1;
            
            return 0;
        })
        .map(([id, arrow]) => ({id, ...arrow}));
}

function updateTrackerArrow(deviceId, quaternion) {
    const arrow = trackerArrows.get(deviceId);
    if (!arrow) return;

    // Create sorted array of tracker arrows with glove first
    const sortedTrackerArrows = getSortedTrackerArrows(trackerArrows);

    const thisIndex = sortedTrackerArrows.findIndex(arrow => arrow.id === deviceId);

    // Store the quaternion in the arrow object
    arrow.quaternion = quaternion;
    
    // Calculate forward direction with variable length (first tracker gets shorter ray)
    const rayLength = thisIndex === 0 ? 2.0 : 5.0;
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
        wristRotation = calculateRollAroundForward(forwardTip, upTip);
        updateArmValuesDisplay();

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
                    wristFlexion = flexionAngle;
                    wristDeviation = -deviationAngle;
                    updateArmValuesDisplay();

                } else if (thisIndex === 1 && otherIndex === 2) {

                    elbowFlexion = directAngle;
                    updateArmValuesDisplay();

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
                    shoulderFlexion = Math.acos(Math.max(-1, Math.min(1, flexionDot))) * (180 / Math.PI);
                    // Make flexion negative when pointing down
                    if (forward.y < 0) shoulderFlexion = -shoulderFlexion;

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
                    shoulderDeviation = Math.acos(Math.max(-1, Math.min(1, deviationDot))) * (180 / Math.PI);
                    // Make deviation negative when pointing left
                    // if (forward.z < 0) shoulderDeviation = -shoulderDeviation;

                    updateArmValuesDisplay();
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
let wristFlexion = 0;
let wristDeviation = 0;
let wristRotation = 0;
let elbowFlexion = 0;
let shoulderFlexion = 0;
let shoulderDeviation = 0;

// Function to update arm values display
function updateArmValuesDisplay() {
    document.getElementById('wrist-flexion').textContent = `${wristFlexion.toFixed(1)}°`;
    document.getElementById('wrist-deviation').textContent = `${wristDeviation.toFixed(1)}°`;
    document.getElementById('wrist-rotation').textContent = `${wristRotation.toFixed(1)}°`;
    document.getElementById('elbow-flexion').textContent = `${elbowFlexion.toFixed(1)}°`;
    document.getElementById('shoulder-flexion').textContent = `${shoulderFlexion.toFixed(1)}°`;
    document.getElementById('shoulder-deviation').textContent = `${shoulderDeviation.toFixed(1)}°`;

    // Get the first glove from the hands Map
    const firstGloveId = Array.from(hands.keys()).find(id => id.includes('Eidon-Glove'));
    const handModel = firstGloveId ? hands.get(firstGloveId) : null;

    if (handModel && handModel.arm && handModel.arm.wrist && handModel.arm.elbow) {
        // Convert degrees to radians
        const flexionRad = THREE.MathUtils.degToRad(-wristFlexion + 90);
        const deviationRad = THREE.MathUtils.degToRad(-wristDeviation);
        const rotationRad = THREE.MathUtils.degToRad(-wristRotation);
        const elbowRad = THREE.MathUtils.degToRad(-elbowFlexion);
        const shoulderFlexionRad = THREE.MathUtils.degToRad(-shoulderFlexion);
        const shoulderDeviationRad = THREE.MathUtils.degToRad(-shoulderDeviation);
        
        // Apply rotations to the wrist joint
        const wristJoint = handModel.arm.wrist;
        // Reset rotation first
        wristJoint.rotation.set(0, 0, 0);
        // Apply flexion (around X axis)
        wristJoint.rotateX(flexionRad);
        // Apply deviation (around Y axis)
        wristJoint.rotateY(deviationRad);
        // Apply rotation (around Z axis)
        wristJoint.rotateZ(rotationRad);

        // Apply elbow flexion (around Y axis)
        const elbowJoint = handModel.arm.elbow;
        elbowJoint.rotation.set(0, 0, 0);
        elbowJoint.rotateX(elbowRad);

        // Apply shoulder flexion (around X axis)
        const shoulderJoint = handModel.arm.shoulder;
        shoulderJoint.rotation.set(0, 0, 0);
        shoulderJoint.rotateX(shoulderFlexionRad);
        // Apply shoulder deviation (around Z axis)
        shoulderJoint.rotateZ(-shoulderDeviationRad);
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
        return;
    }

    // Build sorted array with same ordering logic used in updateTrackerArrow
    const sortedTrackerArrows = getSortedTrackerArrows(trackerArrows);

    // Position arrows so that last one is at origin and previous arrows chain outwards
    let cumulativeOffset = { x: 2, y: 5, z: 0 };
    for (let i = sortedTrackerArrows.length - 1; i >= 0; i--) {
        const arrow = sortedTrackerArrows[i];
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
            toggle.textContent = chainMode ? 'Chain Mode: On' : 'Chain Mode: Off';
            repositionTrackerArrows();
        };
        return;
    }
    toggle = document.createElement('button');
    toggle.id = 'vector-mode-toggle';
    toggle.textContent = 'Chain Mode: Off';
    toggle.onclick = () => {
        chainMode = !chainMode;
        toggle.textContent = chainMode ? 'Chain Mode: On' : 'Chain Mode: Off';
        repositionTrackerArrows();
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
}

// Call this after dark mode toggle setup
addVectorModeToggle();

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

// Call global calibration button setup after vector mode toggle
addGlobalCalibrationButton();
