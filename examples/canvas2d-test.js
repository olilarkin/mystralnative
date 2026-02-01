// examples/internal/impact-test/canvas2d-test.ts
console.log("[Canvas2D Test] Starting...");
var ctx = canvas.getContext("2d");
if (!ctx) {
  console.error("[Canvas2D Test] Failed to get 2D context");
} else {
  console.log("[Canvas2D Test] Got 2D context");
}
ctx.fillStyle = "#1a1a2e";
ctx.fillRect(0, 0, canvas.width, canvas.height);
console.log("[Canvas2D Test] fillRect - OK");
ctx.save();
ctx.fillStyle = "#ff0000";
ctx.fillRect(50, 50, 100, 100);
ctx.restore();
console.log("[Canvas2D Test] save/restore - OK");
ctx.save();
ctx.translate(200, 100);
ctx.scale(-1, 1);
ctx.fillStyle = "#00ff00";
ctx.fillRect(0, 0, 50, 50);
ctx.restore();
console.log("[Canvas2D Test] scale/translate - OK");
console.log("[Canvas2D Test] Testing drawImage...");
try {
  const offscreen = document.createElement("canvas");
  offscreen.width = 32;
  offscreen.height = 32;
  const offCtx = offscreen.getContext("2d");
  if (offCtx) {
    offCtx.fillStyle = "#0088ff";
    offCtx.fillRect(0, 0, 32, 32);
    if (typeof ctx.drawImage === "function") {
      ctx.drawImage(offscreen, 300, 50);
      console.log("[Canvas2D Test] drawImage - OK");
    } else {
      console.error("[Canvas2D Test] drawImage - MISSING");
    }
  }
} catch (e) {
  console.error("[Canvas2D Test] drawImage error:", e);
}
console.log("[Canvas2D Test] Testing getImageData...");
try {
  const imageData = ctx.getImageData(0, 0, 10, 10);
  console.log("[Canvas2D Test] getImageData - OK, data length:", imageData.data.length);
} catch (e) {
  console.error("[Canvas2D Test] getImageData error:", e);
}
console.log("[Canvas2D Test] Testing putImageData...");
try {
  if (typeof ctx.putImageData === "function") {
    const imageData = ctx.getImageData(50, 50, 20, 20);
    ctx.putImageData(imageData, 400, 50);
    console.log("[Canvas2D Test] putImageData - OK");
  } else {
    console.error("[Canvas2D Test] putImageData - MISSING");
  }
} catch (e) {
  console.error("[Canvas2D Test] putImageData error:", e);
}
console.log("[Canvas2D Test] Testing createImageData...");
try {
  if (typeof ctx.createImageData === "function") {
    const imageData = ctx.createImageData(10, 10);
    console.log("[Canvas2D Test] createImageData - OK");
  } else {
    console.error("[Canvas2D Test] createImageData - MISSING");
  }
} catch (e) {
  console.error("[Canvas2D Test] createImageData error:", e);
}
ctx.fillStyle = "#ffffff";
ctx.font = "20px sans-serif";
ctx.fillText("Canvas 2D API Test", 50, 200);
ctx.fillText("Check console for results", 50, 230);
function animate() {
  requestAnimationFrame(animate);
}
animate();
console.log("[Canvas2D Test] Complete - check visual output");
