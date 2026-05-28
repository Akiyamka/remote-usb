export type PathDescriptor = string | readonly string[];

export function encodePath(pathDescriptor: PathDescriptor, allowRoot: boolean): string {
  return normalizePath(pathDescriptor, allowRoot).map(encodeURIComponent).join('/');
}

export function joinPath(pathDescriptor: PathDescriptor, allowRoot: boolean): string {
  return normalizePath(pathDescriptor, allowRoot).join('/');
}

export function normalizePath(pathDescriptor: PathDescriptor, allowRoot: boolean): string[] {
  const parts = typeof pathDescriptor === 'string' ? pathDescriptor.split('/') : [...pathDescriptor];

  if (parts.length === 0 || (parts.length === 1 && parts[0] === '')) {
    if (allowRoot) {
      return [];
    }

    throw new TypeError('Path must not be empty');
  }

  for (const part of parts) {
    if (part === '' || part.includes('..')) {
      throw new TypeError(`Invalid path component: ${part}`);
    }
  }

  return parts;
}
