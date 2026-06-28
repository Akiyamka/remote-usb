import { device } from '../../appState.js';
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
          Web UI API mismatch: expected v{mismatch.expected}, device reports v{mismatch.actual}.
          Rebuild and reflash the web UI partition.
        </div>
      )}
      {errorMessage !== null && <div class={`${styles['banner']} ${styles['error']}`}>{errorMessage}</div>}
      {isConnecting && <div class={styles['banner']}>Connecting to device...</div>}
    </div>
  );
}
