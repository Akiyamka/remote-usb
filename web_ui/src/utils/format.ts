import { currentLocale, t } from '../i18n.js';

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
    return t('error.unknown');
  }

  return new Intl.DateTimeFormat(currentLocale, {
    dateStyle: 'medium',
    timeStyle: 'short',
  }).format(new Date(mtime * 1000));
}

const relativeTimeFormatter = new Intl.RelativeTimeFormat(currentLocale, {
  numeric: 'auto',
});

const relativeTimeUnits = [
  ['year', 365 * 24 * 60 * 60],
  ['month', 30 * 24 * 60 * 60],
  ['week', 7 * 24 * 60 * 60],
  ['day', 24 * 60 * 60],
  ['hour', 60 * 60],
  ['minute', 60],
  ['second', 1],
] as const satisfies ReadonlyArray<readonly [Intl.RelativeTimeFormatUnit, number]>;

export function formatRelativeMTime(mtime: number | null | undefined): string {
  if (mtime === null || mtime === undefined || !Number.isFinite(mtime)) {
    return t('error.unknown');
  }

  const diffSeconds = mtime - Date.now() / 1000;
  const absDiffSeconds = Math.abs(diffSeconds);
  const [unit, seconds] = relativeTimeUnits.find(([, value]) => absDiffSeconds >= value) ?? [
    'second',
    1,
  ];
  const formatted = relativeTimeFormatter.format(Math.round(diffSeconds / seconds), unit);

  return formatted.charAt(0).toUpperCase() + formatted.slice(1);
}
