import { fileManager } from '../../../appState.js';
import styles from './index.module.css';

export function LoadingIndicator() {
  return (
    <div class={`${styles['root']} ${fileManager.$isLoading.value ? styles['active'] : ''}`} aria-hidden="true">
      <div class={styles['bar']} />
    </div>
  );
}
