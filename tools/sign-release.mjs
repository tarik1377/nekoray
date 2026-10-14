import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

export async function signReleaseDirectory({ directory, tag, privateKeyPem, trustedKeys }) {
  if (!/^v\d{1,3}\.\d\.\d{1,2}$/.test(tag || '')) throw new Error('Invalid release tag or unsupported version code');
  if (!privateKeyPem) throw new Error('Release signing key is missing');
  const privateKey = crypto.createPrivateKey(privateKeyPem);
  if (privateKey.asymmetricKeyType !== 'ed25519') throw new Error('Release signing key must be Ed25519');
  const publicKey = crypto.createPublicKey(privateKey);
  const raw = publicKey.export({ type: 'spki', format: 'der' }).subarray(-32);
  const signingKeyId = `gr-${crypto.createHash('sha256').update(raw).digest('hex').slice(0, 16)}`;
  if (trustedKeys[signingKeyId] !== raw.toString('base64')) throw new Error('Release signing key is not trusted by installed clients');
  const names = fs.readdirSync(directory).filter(name => /^GreenRhythm-.*\.(zip|dmg)$/.test(name)).sort();
  if (!names.length) throw new Error('No release packages');
  const [major, minor, patch] = tag.slice(1).split('.').map(Number);
  const versionCode = major * 1000 + minor * 100 + patch;
  const manifests = [];
  // Validate the entire set before producing any signatures.
  for (const name of names) {
    const match = /^GreenRhythm-(v\d+\.\d+\.\d+)-(windows-x64\.zip|macos-(arm64|amd64)\.dmg)$/.exec(name);
    if (!match || match[1] !== tag) throw new Error(`Unexpected package name: ${name}`);
    const archive = path.join(directory, name);
    const stat = fs.lstatSync(archive);
    if (!stat.isFile() || stat.isSymbolicLink()) throw new Error(`Package must be a regular file: ${name}`);
    if (stat.size <= 0 || stat.size > 1073741824) throw new Error(`Invalid package size: ${name}`);
    const platform = match[2].startsWith('windows') ? 'windows-x64' : `macos-${match[3]}`;
    const hash = crypto.createHash('sha256');
    for await (const chunk of fs.createReadStream(archive)) hash.update(chunk);
    const sha256 = hash.digest('hex');
    const checksumFile = `${archive}.sha256`;
    if (!fs.existsSync(checksumFile)) throw new Error(`Build checksum missing: ${name}`);
    const checksumStat = fs.lstatSync(checksumFile);
    if (!checksumStat.isFile() || checksumStat.isSymbolicLink() || checksumStat.size > 1024) throw new Error(`Invalid build checksum file: ${name}`);
    const expected = /^([a-f0-9]{64})[ \t]+\*?([^\r\n]+)$/.exec(fs.readFileSync(checksumFile, 'utf8').trim());
    if (!expected || expected[2] !== name || expected[1] !== sha256) throw new Error(`Package differs from build checksum: ${name}`);
    manifests.push({ platform, versionCode, version: tag.slice(1), url: `https://verdantvibe.ru/downloads/${encodeURIComponent(name)}`, sha256, sizeBytes: stat.size, mandatory: false, signatureAlgorithm: 'ed25519-v1', signingKeyId });
  }
  const outputs = manifests.map(m => {
    const payload = Buffer.from(['GreenRhythm release v1', signingKeyId, m.platform, m.versionCode, m.version, m.url, m.sha256, m.sizeBytes, '0', ''].join('\n'), 'utf8');
    const signed = { ...m, signature: crypto.sign(null, payload, privateKey).toString('base64') };
    if (!crypto.verify(null, payload, publicKey, Buffer.from(signed.signature, 'base64'))) throw new Error('Signature self-check failed');
    const name = decodeURIComponent(new URL(m.url).pathname.split('/').at(-1));
    const filename = path.join(directory, `${name}.manifest.json`);
    if (fs.existsSync(filename)) {
      const existing = JSON.parse(fs.readFileSync(filename, 'utf8'));
      if (Object.keys(signed).some(field => existing[field] !== signed[field])) throw new Error(`A different manifest already exists: ${name}`);
    }
    return { filename, signed };
  });
  for (const { filename, signed } of outputs) {
    if (!fs.existsSync(filename)) fs.writeFileSync(filename, `${JSON.stringify(signed, null, 2)}\n`, { flag: 'wx' });
  }
  return outputs.map(output => output.signed);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const { values } = parseArgs({ options: { directory: { type: 'string' }, tag: { type: 'string' } } });
    if (!values.directory || !values.tag) throw new Error('Required: --directory <packages> --tag <release tag>');
    const trustedKeys = JSON.parse(fs.readFileSync(new URL('../go/release_auth/trusted_keys.json', import.meta.url), 'utf8'));
    const privateKeyPem = process.env.GREENRHYTHM_RELEASE_PRIVATE_KEY;
    delete process.env.GREENRHYTHM_RELEASE_PRIVATE_KEY;
    const manifests = await signReleaseDirectory({ directory: path.resolve(values.directory), tag: values.tag, privateKeyPem, trustedKeys });
    for (const m of manifests) console.log(`Signed ${m.platform} ${m.version}, key ${m.signingKeyId}`);
  } catch (error) {
    // Avoid leaking a secret through OpenSSL error stacks or command arguments.
    console.error(`Release signing failed: ${error.message}`);
    process.exitCode = 1;
  }
}
