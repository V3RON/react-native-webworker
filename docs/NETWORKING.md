# Networking in web workers

The `react-native-webworker` library includes built-in networking support for your worker threads. Every worker automatically gets the familiar browser `fetch()` API.

## What it does

Every worker comes with a complete Fetch API implementation built-in. Behind the scenes, it uses the platform's native networking:

- **iOS**: Uses `NSURLSession` for HTTP requests
- **Android**: Uses `OkHttp` for HTTP requests

This gives you good performance and handles all the platform-specific stuff like SSL certificates, proxy settings, and connection reuse.

## Fetch API

Workers include a basic Fetch API for making HTTP requests. It's a simplified version compared to the full browser implementation.

### Basic usage

Here's how to make requests:

```typescript
// Simple GET request
const response = await fetch('https://jsonplaceholder.typicode.com/todos/1');
const data = await response.json();
console.log(data);

// POST request with JSON data
const response = await fetch('https://jsonplaceholder.typicode.com/posts', {
  method: 'POST',
  headers: {
    'Content-Type': 'application/json'
  },
  body: JSON.stringify({
    title: 'foo',
    body: 'bar',
    userId: 1
  })
});

if (response.ok) {
  const result = await response.json();
  console.log(result);
}
```

### Supported options

Currently, these fetch options work:

```typescript
const response = await fetch('https://api.example.com/data', {
  method: 'POST', // HTTP method like 'GET', 'POST', 'PUT', 'DELETE'
  headers: {
    'Authorization': 'Bearer token123',
    'Content-Type': 'application/json'
  },
  body: JSON.stringify({ key: 'value' }), // String or ArrayBuffer
  timeout: 5000, // Request timeout in milliseconds
  redirect: 'follow', // 'follow', 'error', or 'manual' (Android only currently)
  mode: 'cors' // Accepted but ignored (Native requests are not subject to CORS)
});
```

**Note**: Advanced options like `credentials` and `signal` are not supported yet.

### Response format

The `fetch()` function returns a basic response object with these properties:

```typescript
const response = await fetch('https://api.example.com/data');

// Basic response info
console.log(response.status); // 200
console.log(response.ok); // true (quick way to check if status is 200-299)

// Get response headers (as a plain object)
console.log(response.headers['content-type']); // 'application/json'

// Read the response body
const text = await response.text(); // Get as plain text
const json = await response.json(); // Parse as JSON
const arrayBuffer = await response.arrayBuffer(); // Get as ArrayBuffer
```

## Handling errors

Fetch provides basic error handling:

### Fetch error handling

```typescript
try {
  const response = await fetch('https://api.example.com/data');

  if (!response.ok) {
    // Check for HTTP errors (like 404, 500, etc.)
    throw new Error(`HTTP ${response.status}: ${response.statusText}`);
  }

  const data = await response.json();
  console.log(data);

} catch (error) {
  // Network errors (DNS failure, connection issues, etc.)
  console.error('Request failed:', error.message);
}
```

## Request body types

You can send different types of data in your requests:

### Text and JSON

```typescript
// Plain text
await fetch('/api', {
  method: 'POST',
  body: 'Hello World'
});

// JSON objects (automatically converted to strings)
await fetch('/api', {
  method: 'POST',
  body: { message: 'Hello' } // Gets turned into '{"message":"Hello"}'
});
```

### Binary data

```typescript
// ArrayBuffer
const buffer = new ArrayBuffer(1024);
await fetch('/api/upload', {
  method: 'POST',
  body: buffer
});

// Typed arrays
const uint8Array = new Uint8Array([1, 2, 3, 4]);
await fetch('/api/upload', {
  method: 'POST',
  body: uint8Array
});
```

## Current limitations

1. **Limited Fetch Options**: Advanced options like `credentials` and `signal` aren't supported yet.

2. **No XMLHttpRequest**: The XMLHttpRequest API isn't implemented.

3. **No AbortController**: Request cancellation isn't available yet.

4. **No Request/Response Classes**: You can't create `new Request()` or `new Response()` objects - just use the basic `fetch()` function.

5. **No Headers Class**: Headers are plain JavaScript objects, not Header class instances.

6. **No FormData**: Multipart form data isn't supported - use JSON or URL-encoded strings instead.

7. **No WebSocket Support**: Only HTTP requests work, no WebSocket connections.
