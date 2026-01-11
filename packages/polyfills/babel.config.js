module.exports = {
  presets: [
    [
      '@react-native/babel-preset',
      {
        // Use hermes-stable profile to ensure all incompatible constructs are transformed
        unstable_transformProfile: 'hermes-stable',
      },
    ],
  ],
};
