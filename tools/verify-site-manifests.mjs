// Verification-only workflow: compare the freshly CI-signed existing packages
// with the manifests served to installed clients. Never changes release data.
import fs from 'node:fs';
import path from 'node:path';
import { parseArgs } from 'node:util';
const { values } = parseArgs({ options: { directory: { type: 'string' } } });
if (!values.directory) throw new Error('Required: --directory <signed packages>');
const manifests = fs.readdirSync(values.directory).filter(name => name.endsWith('.manifest.json'));
if (!manifests.length) throw new Error('No signed manifests to verify');
for (const name of manifests) {
  const local = JSON.parse(fs.readFileSync(path.join(values.directory, name), 'utf8'));
  if (!['windows-x64', 'macos-arm64', 'macos-amd64'].includes(local.platform)) throw new Error('Unexpected platform');
  const response = await fetch(`https://verdantvibe.ru/api/app/version/${local.platform}`, { signal: AbortSignal.timeout(15000) });
  if (!response.ok) throw new Error(`Website returned ${response.status}`);
  const published = await response.json();
  for (const field of Object.keys(local)) {
    if (local[field] !== published[field]) throw new Error(`Website ${local.platform} differs in ${field}`);
  }
  console.log(`Website signature agrees with CI: ${local.platform} ${local.version}`);
}
