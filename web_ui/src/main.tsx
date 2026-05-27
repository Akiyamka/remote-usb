import { render } from 'preact';
import { LocationProvider, Router, Route } from 'preact-iso';

import { Header } from './components/Header.jsx';
import { StatusOverlay } from '#components/StatusOverlay/index.jsx';
import { NotFound } from './pages/_404.jsx';
import { FileManagerPage } from './pages/FileManager/FileManagerPage.jsx';
import { Device } from '#models/Device/Device.jsx';
import { RPC } from './RPCAPI.js';
import { FileManager } from '#models/FileManager/FileManager.jsx';
import { InitialPage } from './pages/InitialPage/index.jsx';
import './style.css';
import { EmulationPage } from './pages/EmulationPage/index.jsx';

const rpc = new RPC();
const device = new Device(rpc);
const fileManager = new FileManager(rpc);

export function App() {
  return (
    <LocationProvider>
      <Header />
      <main>
        <StatusOverlay />
        <Router>
          <Route
            path="/"
            component={
              device.$currentMode.value === 'remote_access'
                ? FileManagerPage
                : device.$currentMode.value === 'drive_emulation'
                  ? EmulationPage
                  : InitialPage
            }
          />
          {/*<Route path="/settings" component={Settings} />*/}
          <Route default component={NotFound} />
        </Router>
      </main>
    </LocationProvider>
  );
}

render(<App />, document.getElementById('app')!);

device.connect().then(() => {
  fileManager.openLastDir();
});
