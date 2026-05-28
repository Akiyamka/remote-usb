import { useState } from 'preact/hooks';
import { device, fileManager } from '../../../appState.js';
import { RPCError } from '../../../RPCAPI.js';
import styles from './index.module.css';
import sharedStyles from '../shared.module.css';

export function DoneButton() {
  const [isSwitching, setIsSwitching] = useState(false);
  const [message, setMessage] = useState<string | null>(null);

  const switchToStorageMode = async () => {
    if (fileManager.hasActiveUploads()) {
      setMessage('Uploads are still running.');
      return;
    }

    setIsSwitching(true);
    setMessage(null);

    try {
      await device.toggleState();
    } catch (error) {
      const reason = error instanceof RPCError && error.code !== undefined ? ` (${error.code})` : '';
      setMessage(
        fileManager.$hasTransferredFiles.value
          ? `The device rejected the mode switch after file transfer${reason}. Try again in a moment.`
          : `The device rejected the mode switch${reason}.`,
      );
    } finally {
      setIsSwitching(false);
    }
  };

  return (
    <div class={styles['control']}>
      <button class={sharedStyles['textAction']} disabled={isSwitching} type="button" onClick={switchToStorageMode}>
        {isSwitching ? 'Switching...' : 'Done'}
      </button>
      {message !== null && <div class={styles['message']}>{message}</div>}
    </div>
  );
}
