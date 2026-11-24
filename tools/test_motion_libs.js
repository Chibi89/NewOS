/**
 * tools/test_motion_libs.js
 * Runner unit test test-by-func per DXBLIT, NSMOTION e NSCOMP
 */
const { execSync } = require('child_process');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const clang = path.join(ROOT, 'tools', 'llvm-mingw', 'llvm-mingw-20260616-ucrt-x86_64', 'bin', 'clang.exe');
const tmpDir = path.join(ROOT, 'build', 'obj');
if (!fs.existsSync(tmpDir)) fs.mkdirSync(tmpDir, { recursive: true });

const suites = [
  {
    name: 'DXBLIT',
    srcs: ['SystemLibrary/blit/dxblit_simd.c', 'SystemLibrary/blit/dxblit_dirty.c', 'SystemLibrary/blit/dxblit_fx.c', 'SystemLibrary/blit/dxblit_test.c'],
    inc: 'SystemLibrary/blit'
  },
  {
    name: 'NSMOTION',
    srcs: ['SystemLibrary/motion/nsmotion_easing.c', 'SystemLibrary/motion/nsmotion_timeline.c', 'SystemLibrary/motion/nsmotion_types.c', 'SystemLibrary/motion/nsmotion_test.c'],
    inc: 'SystemLibrary/motion'
  },
  {
    name: 'NSCOMP',
    srcs: ['SystemLibrary/compositor/nscomp_surface.c', 'SystemLibrary/compositor/nscomp_effects.c', 'SystemLibrary/compositor/nscomp_refraction.c', 'SystemLibrary/compositor/nscomp_test.c'],
    inc: 'SystemLibrary/compositor'
  }
];

let failed = 0;
for (const s of suites) {
  const exe = path.join(tmpDir, `${s.name.toLowerCase()}_test.exe`);
  const srcArgs = s.srcs.map(f => `"${path.join(ROOT, f)}"`).join(' ');
  try {
    execSync(`"${clang}" -O2 -Wall -Wextra -I"${path.join(ROOT, s.inc)}" ${srcArgs} -o "${exe}"`, { stdio: 'inherit' });
    execSync(`"${exe}"`, { stdio: 'inherit' });
  } catch (e) {
    console.error(`[FAIL] ${s.name} test-by-func`);
    failed++;
  }
}

if (failed > 0) process.exit(1);
console.log('=== TUTTI I TEST-BY-FUNC (DXBLIT, NSMOTION, NSCOMP) PASSATI ===');
