/**
 * Three.js WebGPU Demo for Social Media
 *
 * Spinning cube with text overlay using plane geometry
 */

import * as THREE from 'three/webgpu';

async function main() {
  console.log('[Three.js] Starting demo...');

  const renderer = new THREE.WebGPURenderer({
    canvas: canvas,
    antialias: true,
  });

  await renderer.init();
  console.log('[Three.js] WebGPU initialized');

  const width = canvas.width || 1280;
  const height = canvas.height || 720;
  renderer.setSize(width, height, false);
  renderer.setPixelRatio(1);

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x1a1a2e);

  const camera = new THREE.PerspectiveCamera(75, width / height, 0.1, 1000);
  camera.position.z = 4;

  // Create a nice green cube with PBR material
  const geometry = new THREE.BoxGeometry(1.5, 1.5, 1.5);
  const material = new THREE.MeshStandardMaterial({
    color: 0x00ff88,
    metalness: 0.4,
    roughness: 0.3,
  });
  const cube = new THREE.Mesh(geometry, material);
  cube.position.y = -0.2;
  scene.add(cube);

  // Add ambient light
  const ambientLight = new THREE.AmbientLight(0xffffff, 0.6);
  scene.add(ambientLight);

  // Add directional light
  const directionalLight = new THREE.DirectionalLight(0xffffff, 1.2);
  directionalLight.position.set(5, 5, 5);
  scene.add(directionalLight);

  // Add a second light from the other side
  const directionalLight2 = new THREE.DirectionalLight(0x8888ff, 0.5);
  directionalLight2.position.set(-5, 3, -5);
  scene.add(directionalLight2);

  // Create text plane using canvas texture
  function createTextPlane(text, fontSize = 64, color = '#ffffff', width = 1024, height = 128) {
    const canvas2d = document.createElement('canvas');
    canvas2d.width = width;
    canvas2d.height = height;
    const ctx = canvas2d.getContext('2d');

    // Clear with transparency
    ctx.clearRect(0, 0, canvas2d.width, canvas2d.height);

    // Draw text
    ctx.font = `bold ${fontSize}px Arial, sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';

    // Shadow
    ctx.shadowColor = '#000000';
    ctx.shadowBlur = 8;
    ctx.shadowOffsetX = 3;
    ctx.shadowOffsetY = 3;

    // Stroke
    ctx.strokeStyle = '#000000';
    ctx.lineWidth = 6;
    ctx.strokeText(text, canvas2d.width / 2, canvas2d.height / 2);

    // Fill
    ctx.shadowBlur = 0;
    ctx.shadowOffsetX = 0;
    ctx.shadowOffsetY = 0;
    ctx.fillStyle = color;
    ctx.fillText(text, canvas2d.width / 2, canvas2d.height / 2);

    const texture = new THREE.CanvasTexture(canvas2d);
    texture.needsUpdate = true;

    const planeGeometry = new THREE.PlaneGeometry(width / 128, height / 128);
    const planeMaterial = new THREE.MeshBasicMaterial({
      map: texture,
      transparent: true,
      side: THREE.DoubleSide,
    });

    const plane = new THREE.Mesh(planeGeometry, planeMaterial);
    return plane;
  }

  // Main title
  const titlePlane = createTextPlane('Three.js works on', 72, '#ffffff', 1024, 128);
  titlePlane.position.set(0, 2.0, 0);
  scene.add(titlePlane);

  // Subtitle
  const subtitlePlane = createTextPlane('Mystral Native!', 80, '#00ffff', 1024, 128);
  subtitlePlane.position.set(0, 1.2, 0);
  scene.add(subtitlePlane);

  // WebGPU badge
  const badgePlane = createTextPlane('WebGPU Renderer', 48, '#ffff00', 512, 80);
  badgePlane.position.set(0, -2.0, 0);
  scene.add(badgePlane);

  console.log('[Three.js] Scene created, starting render loop...');

  // Animation
  let frameCount = 0;
  function animate() {
    frameCount++;

    // Rotate cube
    cube.rotation.x += 0.015;
    cube.rotation.y += 0.02;

    // Subtle floating motion
    cube.position.y = -0.2 + Math.sin(frameCount * 0.03) * 0.15;

    // Subtle scale pulse
    const scale = 1 + Math.sin(frameCount * 0.05) * 0.05;
    cube.scale.set(scale, scale, scale);

    renderer.render(scene, camera);
    requestAnimationFrame(animate);
  }

  animate();
  console.log('[Three.js] Demo running!');
}

main().catch((e) => {
  console.error('[Three.js] Error:', e.message);
  console.error('[Three.js] Stack:', e.stack);
});
