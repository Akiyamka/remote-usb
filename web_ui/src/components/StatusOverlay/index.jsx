import { device } from '../../appState.js';
import { t } from '../../i18n.js';
import styles from './index.module.css';

export function StatusOverlay() {
  const mismatch = device.$apiVersionMismatch.value;
  const errorMessage = device.$errorMessage.value;
  const isConnecting = device.$isConnecting.value;

  if (mismatch === null && errorMessage === null && !isConnecting) {
    return null;
  }

  return (
    <div class={styles['stack']} aria-live="polite">
      {mismatch !== null && (
        <div class={`${styles['banner']} ${styles['warning']}`}>
          {t('status.apiMismatch', { expected: mismatch.expected, actual: mismatch.actual })}
        </div>
      )}
      {errorMessage !== null && <div class={`${styles['banner']} ${styles['error']}`}>{errorMessage}</div>}
      {isConnecting && <div class={styles['banner']}>{t('app.connecting')}</div>}
    </div>
  );
}
