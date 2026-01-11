const path = require('path');

module.exports = {
  // Project configuration
  projectRoot: __dirname,
  watchFolders: [__dirname],

  // Resolver configuration
  resolver: {
    sourceExts: ['js', 'mjs', 'cjs', 'json'],
    nodeModulesPaths: [path.join(__dirname, 'node_modules')],
  },
};
