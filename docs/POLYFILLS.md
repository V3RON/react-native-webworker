# Polyfills in web workers

The `react-native-webworker` library includes built-in polyfills to provide essential web APIs that are missing from Hermes. Every worker automatically gets these polyfills loaded before your code runs.

## What it does

Every worker comes with polyfills for APIs that Hermes doesn't implement but that web developers expect. Behind the scenes, it uses proven polyfill libraries to provide:

- **AbortController** - Cancel asynchronous operations
- *(Room for future polyfills)*

This ensures your worker code can use modern JavaScript APIs without worrying about Hermes compatibility.

## Adding new polyfills

You can easily extend the polyfill bundle by adding new APIs:

### Adding a polyfill

1. Install the polyfill package:

```bash
cd packages/polyfills
yarn add text-encoding-polyfill
```

2. Import it in the source file:

```javascript
// packages/polyfills/src/index.js
import 'abort-controller/polyfill';
import 'text-encoding-polyfill';  // New polyfill
```

3. Rebuild the polyfills:

```bash
cd packages/polyfills
yarn build
```

The new polyfill will be automatically available in all workers.
