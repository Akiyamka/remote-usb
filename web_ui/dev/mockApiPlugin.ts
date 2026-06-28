/// <reference types="node" />

import type { IncomingMessage, ServerResponse } from 'node:http';
import type { Plugin } from 'vite';

type DeviceMode = 'http' | 'usb' | 'switching';

type MockEntry = MockDir | MockFile;

type MockDir = {
  type: 'dir';
  mtime: number;
};

type MockFile = {
  type: 'file';
  mtime: number;
  contentType: string;
  body: Buffer;
};

const API_VERSION = 1;
const FW_VERSION = '0.0.11-mock';
const TOTAL_BYTES = 32 * 1024 * 1024 * 1024;

export function mockApiPlugin(): Plugin {
  const state = createMockState();

  return {
    name: 'remote-usb-mock-api',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        if (req.url === undefined || !req.url.startsWith('/api/')) {
          next();
          return;
        }

        void handleApiRequest(state, req, res).catch((error: unknown) => {
          sendJson(res, 500, {
            ok: false,
            error: error instanceof Error ? error.message : String(error),
          });
        });
      });
    },
  };
}

function createMockState() {
  const entries = new Map<string, MockEntry>();
  const addDir = (path: string, mtime = nowSeconds()) => entries.set(path, { type: 'dir', mtime });
  const addFile = (path: string, contentType: string, body: string | Buffer, mtime = nowSeconds()) => {
    entries.set(path, {
      type: 'file',
      mtime,
      contentType,
      body: typeof body === 'string' ? Buffer.from(body) : body,
    });
  };

  addDir('');
  addDir('gcode', 1_717_245_660);
  addDir('images', 1_717_245_900);
  addDir('docs', 1_717_246_100);
  addDir('configs', 1_717_246_300);
  addDir('nested', 1_717_246_500);
  addDir('nested/long path with spaces', 1_717_246_700);
  addDir('longlist', 1_717_246_300);

  addFile('readme.txt', 'text/plain', 'Remote USB mock SD card\nEdit the web UI with pnpm dev.\n', 1_717_247_000);
  addFile('configs/wifi.cfg', 'text/plain', 'ssid=MikroTik-12E329\npassword=mock-password\n', 1_717_247_100);
  addFile('configs/printer-profile.json', 'application/json', JSON.stringify({ nozzle: 0.4, bed: 60 }, null, 2), 1_717_247_200);
  addFile('configs/materials.csv', 'text/csv', 'name,temp\nPLA,205\nPETG,235\n', 1_717_247_300);
  addFile('gcode/benchy.gcode', 'text/plain', '; mock benchy\nG28\nG1 X20 Y20 Z0.3 F1500\n', 1_717_247_400);
  addFile('gcode/calibration cube.gcode', 'text/plain', '; calibration cube\nG28\nM104 S205\n', 1_717_247_500);
  addFile('docs/manual.pdf', 'application/pdf', minimalPdf('Remote USB mock manual'), 1_717_247_600);
  addFile('images/dongle.jpg', 'image/jpeg', onePixelJpeg(), 1_717_247_700);
  addFile('images/logo.svg', 'image/svg+xml', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 120 80"><rect width="120" height="80" rx="16" fill="#194bd8"/><text x="60" y="47" text-anchor="middle" fill="white" font-size="24">USB</text></svg>', 1_717_247_800);
  addFile('nested/long path with spaces/notes.md', 'text/markdown', '# Nested file\n\nThis path intentionally contains spaces.\n', 1_717_247_900);

  for (let i = 0; i < 100; i++) {
    addFile(`longlist/file ${i}.txt`, 'text/plain', 'Example text', 1_717_247_700);
  }

  return {
    mode: 'http' as DeviceMode,
    uploadInProgress: false,
    entries,
  };
}

async function handleApiRequest(
  state: ReturnType<typeof createMockState>,
  req: IncomingMessage,
  res: ServerResponse,
) {
  const url = new URL(req.url ?? '/', 'http://mock.local');
  const method = req.method ?? 'GET';

  if (url.pathname === '/api/status' && method === 'GET') {
    const usedBytes = totalUsedBytes(state.entries);
    sendJson(res, 200, {
      fw_version: FW_VERSION,
      api_version: API_VERSION,
      mode: state.mode,
      sd: {
        present: true,
        total_mb: state.mode === 'http' ? Math.round(TOTAL_BYTES / 1024 / 1024) : null,
        free_mb: state.mode === 'http' ? Math.round((TOTAL_BYTES - usedBytes) / 1024 / 1024) : null,
      },
      wifi: {
        connected: true,
        ssid: 'Mock Wi-Fi',
        ip: '192.168.88.234',
        rssi: -42,
      },
    });
    return;
  }

  if (url.pathname === '/api/mode/usb' && method === 'POST') {
    if (state.uploadInProgress) {
      sendBusy(res, 'active_upload');
      return;
    }
    const noChange = state.mode === 'usb';
    state.mode = 'usb';
    sendJson(res, 200, { ok: true, mode: 'usb', ...(noChange ? { no_change: true } : {}) });
    return;
  }

  if (url.pathname === '/api/mode/http' && method === 'POST') {
    const noChange = state.mode === 'http';
    state.mode = 'http';
    sendJson(res, 200, { ok: true, mode: 'http', ...(noChange ? { no_change: true } : {}) });
    return;
  }

  if (url.pathname.startsWith('/api/files') || url.pathname === '/api/mkdir') {
    if (state.mode !== 'http') {
      sendJson(res, 503, { ok: false, error: 'mode_mismatch', current_mode: state.mode });
      return;
    }
  }

  if (url.pathname === '/api/files' && method === 'GET') {
    const relPath = validatePath(url.searchParams.get('path') ?? '', true);
    if (relPath === null) {
      sendInvalidPath(res);
      return;
    }
    sendFileList(state, res, relPath);
    return;
  }

  if (url.pathname.startsWith('/api/files/')) {
    const relPath = validatePath(decodeURIComponent(url.pathname.slice('/api/files/'.length)), false);
    if (relPath === null) {
      sendInvalidPath(res);
      return;
    }

    if (method === 'GET') {
      sendFileDownload(state, res, relPath);
      return;
    }

    if (method === 'POST') {
      await handleUpload(state, req, res, relPath);
      return;
    }

    if (method === 'DELETE') {
      handleDelete(state, res, relPath);
      return;
    }
  }

  if (url.pathname === '/api/mkdir' && method === 'POST') {
    const relPath = validatePath(url.searchParams.get('path') ?? '', false);
    if (relPath === null) {
      sendInvalidPath(res);
      return;
    }
    mkdirParents(state.entries, relPath);
    sendJson(res, 200, { ok: true });
    return;
  }

  sendJson(res, 404, { ok: false, error: 'not_found' });
}

function sendFileList(state: ReturnType<typeof createMockState>, res: ServerResponse, relPath: string) {
  const dir = state.entries.get(relPath);
  if (dir?.type !== 'dir') {
    sendJson(res, 404, { ok: false, error: 'not_found' });
    return;
  }

  const prefix = relPath === '' ? '' : `${relPath}/`;
  const entries = Array.from(state.entries.entries())
    .filter(([path]) => path !== relPath && path.startsWith(prefix) && !path.slice(prefix.length).includes('/'))
    .map(([path, entry]) => {
      const name = path.slice(prefix.length);
      if (entry.type === 'dir') {
        return { name, type: 'dir', mtime: entry.mtime };
      }
      return { name, type: 'file', sizeKb: entry.body.byteLength, mtime: entry.mtime };
    })
    .sort((left, right) => left.name.localeCompare(right.name));

  sendJson(res, 200, { ok: true, path: relPath, entries });
}

function sendFileDownload(state: ReturnType<typeof createMockState>, res: ServerResponse, relPath: string) {
  const entry = state.entries.get(relPath);
  if (entry?.type !== 'file') {
    sendJson(res, 404, { ok: false, error: 'not_found' });
    return;
  }

  res.statusCode = 200;
  res.setHeader('Content-Type', entry.contentType);
  res.setHeader('Cache-Control', 'no-store');
  res.end(entry.body);
}

async function handleUpload(
  state: ReturnType<typeof createMockState>,
  req: IncomingMessage,
  res: ServerResponse,
  relPath: string,
) {
  if (state.uploadInProgress) {
    sendBusy(res, 'active_upload');
    return;
  }

  state.uploadInProgress = true;
  try {
    const body = await readBody(req);
    const parent = parentPath(relPath);
    mkdirParents(state.entries, parent);
    state.entries.set(relPath, {
      type: 'file',
      mtime: nowSeconds(),
      contentType: contentTypeFromPath(relPath),
      body,
    });
    sendJson(res, 201, { ok: true, path: relPath, size: body.byteLength });
  } finally {
    state.uploadInProgress = false;
  }
}

function handleDelete(state: ReturnType<typeof createMockState>, res: ServerResponse, relPath: string) {
  const entry = state.entries.get(relPath);
  if (entry === undefined) {
    sendJson(res, 404, { ok: false, error: 'not_found' });
    return;
  }

  if (entry.type === 'dir') {
    const prefix = `${relPath}/`;
    if (Array.from(state.entries.keys()).some((path) => path.startsWith(prefix))) {
      sendJson(res, 500, { ok: false, error: 'delete_failed' });
      return;
    }
  }

  state.entries.delete(relPath);
  sendJson(res, 200, { ok: true });
}

function mkdirParents(entries: Map<string, MockEntry>, relPath: string) {
  if (relPath === '') {
    return;
  }

  const parts = relPath.split('/');
  for (let i = 1; i <= parts.length; i += 1) {
    const path = parts.slice(0, i).join('/');
    const existing = entries.get(path);
    if (existing?.type === 'file') {
      throw new Error('mkdir_failed');
    }
    if (existing === undefined) {
      entries.set(path, { type: 'dir', mtime: nowSeconds() });
    }
  }
}

function validatePath(path: string, allowEmpty: boolean): string | null {
  if (path === '') {
    return allowEmpty ? '' : null;
  }

  if (path.startsWith('/') || path.endsWith('/') || path.includes('..') || path.includes('\\')) {
    return null;
  }

  const parts = path.split('/');
  if (parts.some((part) => part === '' || part === '.')) {
    return null;
  }

  return parts.join('/');
}

function parentPath(path: string): string {
  const slash = path.lastIndexOf('/');
  return slash === -1 ? '' : path.slice(0, slash);
}

function readBody(req: IncomingMessage): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    req.on('data', (chunk: Buffer) => chunks.push(chunk));
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

function totalUsedBytes(entries: Map<string, MockEntry>): number {
  return Array.from(entries.values()).reduce(
    (total, entry) => total + (entry.type === 'file' ? entry.body.byteLength : 0),
    0,
  );
}

function sendBusy(res: ServerResponse, reason: string) {
  sendJson(res, 409, { ok: false, error: 'busy', reason });
}

function sendInvalidPath(res: ServerResponse) {
  sendJson(res, 400, { ok: false, error: 'invalid_path' });
}

function sendJson(res: ServerResponse, status: number, body: unknown) {
  res.statusCode = status;
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Cache-Control', 'no-store');
  res.end(JSON.stringify(body));
}

function contentTypeFromPath(path: string): string {
  const lower = path.toLowerCase();
  if (lower.endsWith('.jpg') || lower.endsWith('.jpeg')) return 'image/jpeg';
  if (lower.endsWith('.png')) return 'image/png';
  if (lower.endsWith('.svg')) return 'image/svg+xml';
  if (lower.endsWith('.pdf')) return 'application/pdf';
  if (lower.endsWith('.json')) return 'application/json';
  if (lower.endsWith('.csv')) return 'text/csv';
  if (lower.endsWith('.txt') || lower.endsWith('.gcode') || lower.endsWith('.md')) return 'text/plain';
  return 'application/octet-stream';
}

function minimalPdf(title: string): Buffer {
  return Buffer.from(`%PDF-1.1
1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj
2 0 obj<</Type/Pages/Count 1/Kids[3 0 R]>>endobj
3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 160]/Contents 4 0 R>>endobj
4 0 obj<</Length 58>>stream
BT /F1 18 Tf 32 96 Td (${title}) Tj ET
endstream endobj
trailer<</Root 1 0 R>>
%%EOF
`);
}

function onePixelJpeg(): Buffer {
  return Buffer.from(
    '/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCAAYACADASIAAhEBAxEB/8QAFgABAQEAAAAAAAAAAAAAAAAAAAUH/8QAFhABAQEAAAAAAAAAAAAAAAAAABNh/8QAFwEBAQEBAAAAAAAAAAAAAAAAAAYEBf/EABkRAQACAwAAAAAAAAAAAAAAAAABEwISFP/aAAwDAQACEQMRAD8Az6+l9TL6X1bdbkVKd9L6mX0vp1lSbcuCdtybtYLlwLcjWH//2Q==',
    'base64',
  );
}

function nowSeconds(): number {
  return Math.floor(Date.now() / 1000);
}
