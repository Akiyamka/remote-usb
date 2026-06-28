import { useState } from "preact/hooks";
import { device } from "../../appState.js";
import { RPCError } from "../../RPCAPI.js";
import styles from "./index.module.css";
import { Button } from "#components/Button/index.js";

export function EmulationPage() {
  const [isSwitching, setIsSwitching] = useState(false);
  const [message, setMessage] = useState(/** @type {string | null} */ (null));
  const info = device.$deviceInfo.value;

  const switchToHttp = async () => {
    setIsSwitching(true);
    setMessage(null);

    try {
      await device.toggleState();
    } catch (error) {
      setMessage(modeSwitchErrorMessage(error));
    } finally {
      setIsSwitching(false);
    }
  };

  return (
    <div class={styles["root"]}>
      <div class={styles["card"]}>
        <p class={styles["eyebrow"]}>USB storage mode</p>
        <h1>Card is exposed to the USB host</h1>
        <p>
          File operations are disabled while the host owns the SD card. Switch to HTTP mode only
          when the host is idle or safely ejected.
        </p>
        {info?.wifi.connected === true && <p class={styles["meta"]}>Device IP: {info.wifi.ip}</p>}
        <Button ghost={true} disabled={isSwitching} type="button" onClick={switchToHttp}>
          {isSwitching ? "Switching..." : "Switch to HTTP file manager"}
        </Button>
        {message !== null && <div class={styles["message"]}>{message}</div>}
      </div>
    </div>
  );
}

/**
 * @param {unknown} error
 */
function modeSwitchErrorMessage(error) {
  if (error instanceof RPCError && error.status === 409) {
    if (error.reason === "host_io_active") {
      return "The USB host is still reading or writing. Eject or wait, then try again.";
    }
    if (error.reason === "switch_in_progress") {
      return "Another mode switch is already running. Try again in a moment.";
    }
    return "The device is busy. Try again in a moment.";
  }

  return error instanceof Error ? error.message : String(error);
}
