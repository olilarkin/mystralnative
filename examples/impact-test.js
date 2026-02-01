// examples/internal/impact-test/main.ts
console.log("[Impact.js Test] Starting Canvas 2D game demo...");
var SimpleGame = class {
  ctx;
  time = 0;
  entities = [];
  constructor() {
    this.ctx = canvas.getContext("2d");
    for (let i = 0; i < 10; i++) {
      this.entities.push({
        x: Math.random() * canvas.width,
        y: Math.random() * canvas.height,
        vx: (Math.random() - 0.5) * 4,
        vy: (Math.random() - 0.5) * 4,
        color: `hsl(${Math.random() * 360}, 70%, 60%)`,
        size: 20 + Math.random() * 30
      });
    }
    console.log("[Impact.js Test] SimpleGame initialized with", this.entities.length, "entities");
  }
  update() {
    this.time += 0.016;
    for (const e of this.entities) {
      e.x += e.vx;
      e.y += e.vy;
      if (e.x < 0 || e.x > canvas.width) e.vx *= -1;
      if (e.y < 0 || e.y > canvas.height) e.vy *= -1;
      e.x = Math.max(0, Math.min(canvas.width, e.x));
      e.y = Math.max(0, Math.min(canvas.height, e.y));
    }
  }
  draw() {
    const width = canvas.width;
    const height = canvas.height;
    this.ctx.fillStyle = "#1a1a2e";
    this.ctx.fillRect(0, 0, width, height);
    this.ctx.save();
    this.ctx.translate(width / 2, height / 2);
    this.ctx.rotate(this.time);
    this.ctx.fillStyle = "#00ff88";
    this.ctx.fillRect(-50, -50, 100, 100);
    this.ctx.restore();
    for (const e of this.entities) {
      this.ctx.beginPath();
      this.ctx.arc(e.x, e.y, e.size / 2, 0, Math.PI * 2);
      this.ctx.fillStyle = e.color;
      this.ctx.fill();
    }
    this.ctx.fillStyle = "#ffffff";
    this.ctx.font = "28px sans-serif";
    this.ctx.fillText("Canvas 2D Game Engine Test", 50, 50);
    this.ctx.font = "18px sans-serif";
    this.ctx.fillStyle = "#aaaaaa";
    this.ctx.fillText("Impact.js-style game running on MystralNative", 50, 80);
    this.ctx.fillText(`Entities: ${this.entities.length} | Frame time: ${(this.time * 1e3).toFixed(0)}ms`, 50, 110);
  }
};
var game = new SimpleGame();
function animate() {
  game.update();
  game.draw();
  requestAnimationFrame(animate);
}
animate();
console.log("[Impact.js Test] Animation loop started");
