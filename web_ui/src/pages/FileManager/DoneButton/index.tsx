import { useState } from "preact/hooks";
import { device, fileManager } from "../../../appState.js";
import { t } from "../../../i18n.js";
import { RPCError } from "../../../RPCAPI.js";
import styles from "./index.module.css";
import clsx from "clsx";
import { Button } from "#components/Button/index.js";

export function SwitchModeButton() {
  const [isSwitching, setIsSwitching] = useState(false);
  const [message, setMessage] = useState<string | null>(null);

  const switchToStorageMode = async () => {
    if (fileManager.hasActiveUploads()) {
      setMessage(t("modeSwitch.uploadsRunning"));
      return;
    }

    setIsSwitching(true);
    setMessage(null);

    try {
      await device.toggleState();
    } catch (error) {
      setMessage(
        fileManager.$hasTransferredFiles.value
          ? t("modeSwitch.transferredFilesRemain", { message: modeSwitchErrorMessage(error) })
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
        {isSwitching ? t("modeSwitch.switching") : t("modeSwitch.switchToUsb")}
      </Button>
      {message !== null && <div class={styles["message"]}>{message}</div>}
    </div>
  );
}

function modeSwitchErrorMessage(error: unknown): string {
  if (error instanceof RPCError && error.status === 409) {
    if (error.reason === "active_upload") {
      return t("modeSwitch.uploadsRunning");
    }
    if (error.reason === "host_io_active") {
      return t("modeSwitch.hostIoActive");
    }
    if (error.reason === "switch_in_progress") {
      return t("modeSwitch.switchInProgress");
    }
    return t("modeSwitch.deviceBusy");
  }

  return error instanceof Error ? error.message : String(error);
}
