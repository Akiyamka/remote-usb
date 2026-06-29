import { render } from 'preact';
import { useEffect } from 'preact/hooks';
import { LocationProvider, Router, Route } from 'preact-iso';

import { StatusOverlay } from '#components/StatusOverlay/index.jsx';
import { NotFound } from './pages/_404.jsx';
import { FileManagerPage } from './pages/FileManager/FileManagerPage.jsx';
import { device } from './appState.js';
import { currentLanguage } from './i18n.js';
import type { DeviceMode } from './RPCAPI.js';
import { InitialPage } from './pages/InitialPage/index.jsx';
import './style.css';
import { EmulationPage } from './pages/EmulationPage/index.jsx';

const pngFaviconLinkId = 'device-mode-favicon-png';
const svgFaviconLinkId = 'device-mode-favicon-svg';
const faviconByMode: Record<DeviceMode, { png: string; svg: string }> = {
  http: { png: '/favicon_http.png', svg: '/favicon_http.svg' },
  usb: { png: '/favicon_usb.png', svg: '/favicon_usb.svg' },
  switching: { png: '/favicon_unknown.png', svg: '/favicon_unknown.svg' },
};

export function App() {
  return (
    <LocationProvider>
      <main>
        <DeviceModeFavicon />
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

function DeviceModeFavicon() {
  const mode = device.$currentMode.value;

  useEffect(() => {
    const favicon = faviconByMode[mode];
    syncFaviconLink(pngFaviconLinkId, 'image/png', favicon.png);
    syncFaviconLink(svgFaviconLinkId, 'image/svg+xml', favicon.svg);
  }, [mode]);

  return null;
}

function syncFaviconLink(id: string, type: string, href: string): void {
  let link = document.getElementById(id);

  if (!(link instanceof HTMLLinkElement)) {
    link = document.createElement('link');
    link.id = id;
    link.rel = 'icon';
    document.head.append(link);
  }

  link.type = type;
  link.href = href;
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

document.documentElement.lang = currentLanguage;

render(<App />, document.getElementById('app')!);

void device.connect();
