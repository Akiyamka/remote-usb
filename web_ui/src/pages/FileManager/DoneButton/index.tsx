import { useState } from "preact/hooks";
import { device, fileManager } from "../../../appState.js";
import { RPCError } from "../../../RPCAPI.js";
import styles from "./index.module.css";
import sharedStyles from "../shared.module.css";
import clsx from "clsx";
import { Button } from "#components/Button/index.js";

export function SwitchModeButton() {
  const [isSwitching, setIsSwitching] = useState(false);
  const [message, setMessage] = useState<string | null>(null);

  const switchToStorageMode = async () => {
    if (fileManager.hasActiveUploads()) {
      setMessage("Uploads are still running.");
      return;
    }

    setIsSwitching(true);
    setMessage(null);

    try {
      await device.toggleState();
    } catch (error) {
      setMessage(
        fileManager.$hasTransferredFiles.value
          ? `${modeSwitchErrorMessage(error)} The transferred files are still on the SD card.`
          : modeSwitchErrorMessage(error),
      );
    } finally {
      setIsSwitching(false);
    }
  };

  return (
    <div class={styles["control"]}>
      <Button
        ghost={false}
        class={clsx(styles["doneButton"])}
        disabled={isSwitching}
        type="button"
        onClick={switchToStorageMode}
      >
        {isSwitching ? "Switching..." : "Switch to USB Storage mode"}
      </Button>
      {message !== null && <div class={styles["message"]}>{message}</div>}
    </div>
  );
}

function modeSwitchErrorMessage(error: unknown): string {
  if (error instanceof RPCError && error.status === 409) {
    if (error.reason === "active_upload") {
      return "Uploads are still running.";
    }
    if (error.reason === "host_io_active") {
      return "The USB host is still using the card. Try again after it goes idle.";
    }
    if (error.reason === "switch_in_progress") {
      return "Another mode switch is already running. Try again in a moment.";
    }
    return "The device is busy. Try again in a moment.";
  }

  return error instanceof Error ? error.message : String(error);
}
