// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

import tailwindcss from '@tailwindcss/vite';

// https://astro.build/config
export default defineConfig({
  integrations: [
    starlight({
      title: 'RN WebWorker',
      logo: {
        src: './src/assets/logo.svg',
      },
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/V3RON/react-native-webworker',
        },
      ],

      customCss: ['./src/styles/global.css'],
      sidebar: [
        {
          label: 'Start here',
          items: [{ label: 'Getting started', slug: 'getting-started' }],
        },
        {
          label: 'Guides',
          items: [
            {
              label: 'Workers vs. Worklets',
              slug: 'guides/workers-vs-worklets',
            },
            { label: 'Networking', slug: 'guides/networking' },
            { label: 'Polyfills', slug: 'guides/polyfills' },
          ],
        },
        {
          label: 'Reference',
          autogenerate: { directory: 'reference' },
        },
      ],
    }),
  ],

  vite: {
    plugins: [tailwindcss()],
  },
});
