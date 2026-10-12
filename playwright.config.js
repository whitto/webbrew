const { defineConfig } = require('@playwright/test');
module.exports = defineConfig({
  testDir: './test/browser',
  fullyParallel: false,
  workers: 1,
  use: {
    baseURL: 'http://127.0.0.1:8080',
    launchOptions: process.env.CHROMIUM_PATH ? { executablePath: process.env.CHROMIUM_PATH } : {},
    trace: 'retain-on-failure'
  },
  webServer: { command: 'python3 scripts/demo_server.py', url: 'http://127.0.0.1:8080/?demo=1', reuseExistingServer: !process.env.CI }
});
