/**
 * PixiJS v8 WebGPU Sprite Example (Source)
 *
 * This example demonstrates PixiJS v8 with WebGPU renderer working with MystralNative.
 * It shows Graphics primitives, generated textures, and Sprite rendering.
 *
 * IMPORTANT: PixiJS v8 must use the WebGPU renderer (preference: 'webgpu').
 * The WebGL renderer is NOT supported in MystralNative.
 *
 * REQUIREMENTS:
 *   npm install pixi.js@^8.0.0
 *
 * BUNDLING (required before running):
 *   npx esbuild examples/pixijs-sprite-src.js --bundle --outfile=examples/pixijs-bundle.js --format=esm --platform=browser
 *
 * RUN:
 *   mystral run examples/pixijs-bundle.js
 *
 * Tested with: pixi.js@8.15.0
 */

import { Application, Graphics, Sprite, Container } from 'pixi.js';

async function main() {
  console.log('[PixiJS] Starting PixiJS v8 WebGPU test...');

  // Create application - MUST use WebGPU preference
  const app = new Application();

  await app.init({
    width: 800,
    height: 600,
    backgroundColor: 0x1099bb,
    preference: 'webgpu', // REQUIRED: MystralNative only supports WebGPU
    hello: true, // Logs renderer info to console
  });

  console.log('[PixiJS] Application initialized');
  console.log('[PixiJS] Renderer type:', app.renderer.type); // Should be 2 (WebGPU)

  // Add canvas to document (MystralNative handles this)
  document.body.appendChild(app.canvas);

  // ============================================
  // Example 1: Graphics Primitives
  // ============================================

  // Create a red rectangle
  const rectangle = new Graphics();
  rectangle.rect(0, 0, 150, 100);
  rectangle.fill(0xff0000);
  rectangle.x = 100;
  rectangle.y = 100;
  rectangle.pivot.set(75, 50);
  app.stage.addChild(rectangle);
  console.log('[PixiJS] Added red rectangle');

  // Create a green circle
  const circle = new Graphics();
  circle.circle(0, 0, 60);
  circle.fill(0x00ff00);
  circle.x = 350;
  circle.y = 200;
  app.stage.addChild(circle);
  console.log('[PixiJS] Added green circle');

  // ============================================
  // Example 2: Sprites from Generated Textures
  // ============================================

  // Create a texture using Graphics
  const patternGraphics = new Graphics();
  patternGraphics.rect(0, 0, 80, 80);
  patternGraphics.fill(0xff6600);
  patternGraphics.rect(8, 8, 64, 64);
  patternGraphics.fill(0xffff00);
  patternGraphics.circle(40, 40, 24);
  patternGraphics.fill(0x0066ff);

  // Generate texture from graphics
  const patternTexture = app.renderer.generateTexture(patternGraphics);
  console.log('[PixiJS] Generated pattern texture');

  // Create sprites from the texture
  const spriteContainer = new Container();
  spriteContainer.y = 350;
  app.stage.addChild(spriteContainer);

  for (let i = 0; i < 6; i++) {
    const sprite = new Sprite(patternTexture);
    sprite.x = 80 + i * 110;
    sprite.anchor.set(0.5);
    spriteContainer.addChild(sprite);
  }
  console.log('[PixiJS] Created 6 sprites from generated texture');

  // ============================================
  // Example 3: Bunny-like Sprite
  // ============================================

  const bunnyGraphics = new Graphics();
  // Head
  bunnyGraphics.circle(0, 0, 30);
  bunnyGraphics.fill(0xffffff);
  // Left ear
  bunnyGraphics.ellipse(-12, -40, 6, 20);
  bunnyGraphics.fill(0xffffff);
  // Right ear
  bunnyGraphics.ellipse(12, -40, 6, 20);
  bunnyGraphics.fill(0xffffff);
  // Eyes
  bunnyGraphics.circle(-10, -5, 4);
  bunnyGraphics.fill(0x000000);
  bunnyGraphics.circle(10, -5, 4);
  bunnyGraphics.fill(0x000000);
  // Nose
  bunnyGraphics.ellipse(0, 8, 6, 4);
  bunnyGraphics.fill(0xffcccc);

  const bunnyTexture = app.renderer.generateTexture(bunnyGraphics);
  console.log('[PixiJS] Generated bunny texture');

  // Create bunny sprites
  const bunnyContainer = new Container();
  bunnyContainer.y = 500;
  app.stage.addChild(bunnyContainer);

  for (let i = 0; i < 8; i++) {
    const bunny = new Sprite(bunnyTexture);
    bunny.x = 60 + i * 90;
    bunny.anchor.set(0.5);
    bunny.scale.set(0.7);
    bunnyContainer.addChild(bunny);
  }
  console.log('[PixiJS] Created 8 bunny sprites');

  // ============================================
  // Animation Loop
  // ============================================

  let elapsed = 0;
  app.ticker.add((ticker) => {
    elapsed += ticker.deltaTime;

    // Rotate the rectangle
    rectangle.rotation = elapsed * 0.02;

    // Pulse the circle
    const scale = 1 + Math.sin(elapsed * 0.05) * 0.2;
    circle.scale.set(scale);

    // Animate pattern sprites
    spriteContainer.children.forEach((sprite, index) => {
      if (sprite instanceof Sprite) {
        sprite.rotation = elapsed * 0.03;
        sprite.scale.set(1 + Math.sin(elapsed * 0.05 + index) * 0.15);
      }
    });

    // Animate bunny sprites (wave motion)
    bunnyContainer.children.forEach((bunny, index) => {
      if (bunny instanceof Sprite) {
        bunny.y = Math.sin(elapsed * 0.1 + index * 0.5) * 20;
        bunny.rotation = Math.sin(elapsed * 0.05 + index) * 0.2;
      }
    });
  });

  console.log('[PixiJS] Animation started');
  console.log('[PixiJS] PixiJS v8 WebGPU demo running!');
}

main().catch((e) => {
  console.error('[PixiJS] Error:', e.message);
  console.error('[PixiJS] Stack:', e.stack);
});
