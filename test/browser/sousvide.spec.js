const { test, expect } = require('@playwright/test');
test.beforeEach(async ({ page }) => {
  await page.goto('/?demo=1&mode=sousvide');
  await expect(page.locator('#productName')).toHaveText('SousVide');
});
test('sous-vide branding, targets and cooking controls', async ({ page }) => {
  await expect(page.locator('#page-title')).toHaveText('Cook Dashboard');
  await expect(page.locator('#boilC')).toBeHidden();
  await expect(page.locator('select[aria-label="Stage 1 type"]')).toBeHidden();
  await expect(page.locator('input[aria-label="Stage 1 target"]')).toHaveValue('60.0');
  await expect(page.locator('input[aria-label="Stage 1 cook hours"]')).toHaveValue('2');
  await expect(page.locator('#start')).toHaveText('▶ Preheat bath');
  await expect(page.locator('#cookNote')).toContainText('not food core temperature');
});
test('cook timer waits for food, pauses outside band and finishes off', async ({ page }) => {
  await page.locator('input[aria-label="Stage 1 cook hours"]').fill('0.016666667');
  await page.locator('#saveConfig').click();
  await page.locator('#start').click();
  await expect(page.locator('#stageDetail')).toContainText('cook timer not started');
  await page.evaluate(()=>{ds.temperatureC=60;ds.targetC=60;ds.reached=true;});
  await expect(page.locator('#foodAdded')).toBeVisible();
  const before=await page.evaluate(()=>ds.holdElapsed);expect(before).toBe(0);
  await page.locator('#foodAdded').click();
  await expect(page.locator('#foodPrompt')).toBeHidden();
  await page.evaluate(()=>{ds.temperatureC=59;ds.holdElapsed=5;});
  await expect(page.locator('#stageDetail')).toContainText('outside target band');
  const frozen=await page.evaluate(()=>ds.holdElapsed);expect(frozen).toBe(5);
  await page.evaluate(()=>{ds.temperatureC=60;ds.holdElapsed=59;});
  await expect(page.locator('#stageTitle')).toHaveText('Cook complete');
  await expect(page.locator('#output')).toHaveText('0%');
});
test('long-cook settings, templates and Fahrenheit remain usable', async ({ page }) => {
  await page.locator('[data-page="profiles"]').click();
  await page.locator('#templateSelect').selectOption('long');
  await page.locator('#loadTemplate').click();
  await page.locator('[data-page="dashboard"]').click();
  await expect(page.locator('input[aria-label="Stage 1 cook hours"]')).toHaveValue('8');
  await page.locator('input[aria-label="Stage 1 cook hours"]').fill('24');
  await page.locator('#unit').selectOption('F');
  await expect(page.locator('input[aria-label="Stage 1 target"]')).toHaveValue('149.0');
  await page.locator('#saveConfig').click();
  await expect(page.locator('#start')).toBeEnabled();
});
test('reject boiling target and insufficient cutoff', async ({ page }) => {
  await page.locator('input[aria-label="Stage 1 target"]').fill('100');
  await page.locator('#saveConfig').click();
  await expect(page.locator('#toast')).toContainText('40–90');
  await page.locator('input[aria-label="Stage 1 target"]').fill('90');
  await page.locator('[data-page="settings"]').click();
  await page.locator('#maxC').fill('91');
  await page.locator('#maxC').dispatchEvent('change');
  await page.locator('#saveConfig').click();
  await expect(page.locator('#toast')).toContainText('2 °C above');
});
test('mobile sous-vide layout does not overflow', async ({ page }) => {
  await page.setViewportSize({width:390,height:844});
  expect(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth)).toBe(false);
  await page.locator('#start').click();
  await page.locator('#manualMode').click();
  await expect(page.locator('#manualPower')).toBeEnabled();
  await page.locator('#stop').click();
  await expect(page.locator('#output')).toHaveText('0%');
});
