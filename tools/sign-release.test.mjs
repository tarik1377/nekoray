import test from 'node:test';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { signReleaseDirectory } from './sign-release.mjs';

const { privateKey, publicKey } = crypto.generateKeyPairSync('ed25519');
const raw = publicKey.export({ type: 'spki', format: 'der' }).subarray(-32);
const id = `gr-${crypto.createHash('sha256').update(raw).digest('hex').slice(0, 16)}`;
const keys = { [id]: raw.toString('base64') };
const pem = privateKey.export({ type: 'pkcs8', format: 'pem' });
function fixture(t) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'gr-signing-'));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  for (const suffix of ['windows-x64.zip', 'macos-arm64.dmg', 'macos-amd64.dmg']) {
    const name = `GreenRhythm-v1.9.3-${suffix}`;
    const content = `test ${suffix}`;
    fs.writeFileSync(path.join(directory, name), content);
    fs.writeFileSync(path.join(directory, `${name}.sha256`), `${crypto.createHash('sha256').update(content).digest('hex')}  ${name}\n`);
  }
  return directory;
}
test('signs all three platforms using the exact desktop byte contract', async t => {
  const directory = fixture(t);
  const manifests = await signReleaseDirectory({ directory, tag: 'v1.9.3', privateKeyPem: pem, trustedKeys: keys });
  assert.equal(manifests.length, 3);
  for (const m of manifests) {
    assert.equal(m.versionCode, 1903);
    assert.equal(m.signatureAlgorithm, 'ed25519-v1');
    const expected = ['GreenRhythm release v1', id, m.platform, 1903, '1.9.3', m.url, m.sha256, m.sizeBytes, '0', ''].join('\n');
    assert.ok(crypto.verify(null, Buffer.from(expected), publicKey, Buffer.from(m.signature, 'base64')));
    assert.equal(m.sha256, crypto.createHash('sha256').update(fs.readFileSync(path.join(directory, new URL(m.url).pathname.split('/').at(-1)))).digest('hex'));
  }
  assert.deepEqual(await signReleaseDirectory({ directory, tag: 'v1.9.3', privateKeyPem: pem, trustedKeys: keys }), manifests);
});
test('rejects an unpinned key before writing any manifests', async t => {
  const directory = fixture(t);
  await assert.rejects(signReleaseDirectory({ directory, tag: 'v1.9.3', privateKeyPem: pem, trustedKeys: {} }), /trusted/);
  assert.equal(fs.readdirSync(directory).filter(f => f.endsWith('.manifest.json')).length, 0);
});
test('rejects version collisions, unsafe tags, missing keys, and empty packages', async t => {
  const directory = fixture(t);
  for (const tag of ['v1.10.0', 'v1.9.100', 'v1.9.3;echo bad', 'v1.9.3\n', 'v1.9.3-rc1']) {
    await assert.rejects(signReleaseDirectory({ directory, tag, privateKeyPem: pem, trustedKeys: keys }), /tag/);
  }
  await assert.rejects(signReleaseDirectory({ directory, tag: 'v1.9.3', privateKeyPem: '', trustedKeys: keys }), /key/);
  fs.writeFileSync(path.join(directory, 'GreenRhythm-v1.9.3-windows-x64.zip'), '');
  await assert.rejects(signReleaseDirectory({ directory, tag: 'v1.9.3', privateKeyPem: pem, trustedKeys: keys }), /size/);
});
test('a modified package cannot silently replace an already signed manifest', async t => {
  const directory = fixture(t);
  await signReleaseDirectory({ directory, tag: 'v1.9.3', privateKeyPem: pem, trustedKeys: keys });
  const name = 'GreenRhythm-v1.9.3-windows-x64.zip';
  fs.appendFileSync(path.join(directory, name), 'changed');
  const changedHash = crypto.createHash('sha256').update(fs.readFileSync(path.join(directory, name))).digest('hex');
  fs.writeFileSync(path.join(directory, `${name}.sha256`), `${changedHash}  ${name}\n`);
  await assert.rejects(signReleaseDirectory({ directory, tag: 'v1.9.3', privateKeyPem: pem, trustedKeys: keys }), /different manifest/);
});
test('corrupted packages and missing build checksums fail before any manifests are written', async t => {
  const directory = fixture(t);
  fs.appendFileSync(path.join(directory, 'GreenRhythm-v1.9.3-windows-x64.zip'), 'corrupted');
  await assert.rejects(signReleaseDirectory({ directory, tag: 'v1.9.3', privateKeyPem: pem, trustedKeys: keys }), /build checksum/);
  assert.equal(fs.readdirSync(directory).filter(f => f.endsWith('.manifest.json')).length, 0);
  fs.unlinkSync(path.join(directory, 'GreenRhythm-v1.9.3-macos-arm64.dmg.sha256'));
  await assert.rejects(signReleaseDirectory({ directory, tag: 'v1.9.3', privateKeyPem: pem, trustedKeys: keys }), /checksum missing/);
});
test('foreign versions, symlinks, and directories cannot be signed', async t => {
  const directory = fixture(t);
  fs.writeFileSync(path.join(directory, 'GreenRhythm-v1.9.2-windows-x64.zip'), 'old');
  await assert.rejects(signReleaseDirectory({ directory, tag: 'v1.9.3', privateKeyPem: pem, trustedKeys: keys }), /package name/);
  fs.unlinkSync(path.join(directory, 'GreenRhythm-v1.9.2-windows-x64.zip'));
  const file = path.join(directory, 'GreenRhythm-v1.9.3-macos-arm64.dmg');
  fs.unlinkSync(file);
  fs.mkdirSync(file);
  await assert.rejects(signReleaseDirectory({ directory, tag: 'v1.9.3', privateKeyPem: pem, trustedKeys: keys }), /regular file/);
});
