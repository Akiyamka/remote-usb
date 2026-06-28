import { render } from 'preact';
import { LocationProvider, Router, Route } from 'preact-iso';

import { StatusOverlay } from '#components/StatusOverlay/index.jsx';
import { NotFound } from './pages/_404.jsx';
import { FileManagerPage } from './pages/FileManager/FileManagerPage.jsx';
import { device } from './appState.js';
import { InitialPage } from './pages/InitialPage/index.jsx';
import './style.css';
import { EmulationPage } from './pages/EmulationPage/index.jsx';

export function App() {
  return (
    <LocationProvider>
      <main>
        <StatusOverlay />
        <Router>
          <Route path="/" component={HomePage} />
          {/*<Route path="/settings" component={Settings} />*/}
          <Route default component={NotFound} />
        </Router>
      </main>
    </LocationProvider>
  );
}

function HomePage() {
  const mode = device.$currentMode.value;

  if (mode === 'http') {
    return <FileManagerPage />;
  }

  if (mode === 'usb') {
    return <EmulationPage />;
  }

  return <InitialPage />;
}

render(<App />, document.getElementById('app')!);

void device.connect();
