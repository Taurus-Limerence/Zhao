// Offline regression: execute the real bootstrap with only external browser/SDK boundaries faked.
const {readFileSync} = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = readFileSync(require('node:path').join(__dirname, '../qml/assets/aladin.html'), 'utf8');
const elements = new Map();
const element = id => {
  if (!elements.has(id)) {
    const classes = new Set();
    elements.set(id, {textContent: '', classList: {add: x => classes.add(x), remove: x => classes.delete(x), contains: x => classes.has(x)}});
  }
  return elements.get(id);
};
let polygons = 0;
let layouts = 0, redraws = 0, fovChanges = 0;
const animationFrames = [];
const context = {console, setTimeout: () => 1, clearTimeout() {},
  requestAnimationFrame: callback => animationFrames.push(callback),
  document: {getElementById: element, createElement: () => ({}), head: {appendChild(s) { s.onload(); }}},
  A: {init: Promise.resolve(), aladin: () => ({addOverlay() {}, gotoRaDec() {}, setFoV() {fovChanges++;},
      view: {fixLayoutDimensions() {layouts++;}, requestRedraw() {redraws++;}}}),
      graphicOverlay: () => ({removeAll() {}, addFootprints() {polygons++;}}), polygon: x => x}};
context.window = context;
vm.createContext(context);
// The SDK import is the external boundary; bootstrap against the supplied SDK double.
for (const match of html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g))
  vm.runInContext(match[1].replace('startEngine();', 'boot();'), context);
setImmediate(() => {
  assert.equal(context.stellaReady, 'ready');
  assert.equal(element('fallback').classList.contains('hidden'), true, 'Ready engine must remove loading overlay');
  context.stellaGoto(10, 40, '[[9,39],[11,39],[11,41],[9,41]]', 2);
  assert.equal(polygons, 1, 'Frame must use the supported overlay footprint API');
  const previousFovChanges = fovChanges;
  context.stellaNudgeResize();
  context.stellaNudgeResize();
  assert.equal(animationFrames.length, 1, 'Resize bursts must coalesce until the next browser frame');
  animationFrames.shift()();
  assert.equal(layouts, 1, 'Resize must refresh the actual Aladin canvas geometry');
  assert.equal(redraws, 1, 'Refreshed canvas must request a redraw');
  assert.equal(fovChanges, previousFovChanges, 'Resize must preserve user zoom');
  context.stellaNudgeResize();
  assert.equal(animationFrames.length, 1, 'Showing the same-size view again must schedule another refresh');
  animationFrames.shift()();
  assert.equal(layouts, 2, 'Each return to the view must refresh geometry even without a size change');
  assert.equal(redraws, 2);
  assert.equal(fovChanges, previousFovChanges);
  console.log('Aladin bootstrap regressions passed');
});
