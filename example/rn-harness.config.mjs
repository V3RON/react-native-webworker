import {
  androidPlatform,
  androidEmulator,
} from '@react-native-harness/platform-android';
import {
  applePlatform,
  appleSimulator,
} from '@react-native-harness/platform-apple';

const config = {
  entryPoint: './index.js',
  appRegistryComponentName: 'WebworkerExample',

  runners: [
    androidPlatform({
      name: 'android',
      device: androidEmulator('Pixel_8_API_35', {
        apiLevel: 35,
        profile: 'pixel_6',
        diskSize: '1G',
        heapSize: '1G',
      }),
      bundleId: 'webworker.example',
    }),
    applePlatform({
      name: 'ios',
      device: appleSimulator('iPhone 17 Pro', '26.0'),
      bundleId: 'webworker.example',
    }),
  ],
  defaultRunner: 'android',
};

export default config;
