import { device } from "../../appState.js";

export function EmulationPage() {
  return (
    <div>
      Press this button for enter into remove control mode You can modify files in this state, but
      host device will not able to work with it in this mode. Make sure that device not used right
      now.
      <button onClick={() => device.toggleState()}>Switch to remote control state</button>
    </div>
  );
}
