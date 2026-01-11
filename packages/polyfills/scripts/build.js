const Metro = require('metro');
const path = require('path');
const fs = require('fs');

async function build() {
  const projectRoot = path.join(__dirname, '..');
  const distDir = path.join(projectRoot, 'dist');

  if (!fs.existsSync(distDir)) {
    fs.mkdirSync(distDir, { recursive: true });
  }

  const config = await Metro.loadConfig({
    config: path.join(projectRoot, 'metro.config.js'),
  });

  await Metro.runBuild(config, {
    entry: path.join(projectRoot, 'src/index.js'),
    out: path.join(distDir, 'polyfills.bundle.js'),
    platform: 'ios',
    minify: true,
    dev: false,
  });

  console.log('Bundle created: dist/polyfills.bundle.js');
}

build().catch((error) => {
  console.error('Build failed:', error);
  process.exit(1);
});
