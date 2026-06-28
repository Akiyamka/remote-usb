import { useState } from "preact/hooks";
import { device } from "../../appState.js";
import { t } from "../../i18n.js";
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
        <p class={styles["eyebrow"]}>{t("emulation.eyebrow")}</p>
        <h1>{t("emulation.title")}</h1>
        <p>{t("emulation.description")}</p>
        {info?.wifi.connected === true && (
          <p class={styles["meta"]}>{t("emulation.deviceIp", { ip: info.wifi.ip })}</p>
        )}
        <Button ghost={true} disabled={isSwitching} type="button" onClick={switchToHttp}>
          {isSwitching ? t("modeSwitch.switching") : t("emulation.switchToHttp")}
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
      return t("modeSwitch.hostReadingWriting");
    }
    if (error.reason === "switch_in_progress") {
      return t("modeSwitch.switchInProgress");
    }
    return t("modeSwitch.deviceBusy");
  }

  return error instanceof Error ? error.message : String(error);
}
