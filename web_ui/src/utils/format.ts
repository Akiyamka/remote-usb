export function formatFileSize(bytes: number | null | undefined): string {
  if (!Number.isFinite(bytes) || bytes === null || bytes === undefined || bytes < 0) {
    return '';
  }

  if (bytes < 1024) {
    return `${bytes} B`;
  }

  const units = ['KB', 'MB', 'GB', 'TB'];
  let value = bytes / 1024;
  let unitIndex = 0;

  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }

  const decimals = value >= 10 ? 1 : 2;
  return `${value.toFixed(decimals)} ${units[unitIndex]}`;
}

export function formatMTime(mtime: number | null | undefined): string {
  if (mtime === null || mtime === undefined || !Number.isFinite(mtime)) {
    return 'unknown';
  }

  return new Intl.DateTimeFormat(undefined, {
    dateStyle: 'medium',
    timeStyle: 'short',
  }).format(new Date(mtime * 1000));
}
