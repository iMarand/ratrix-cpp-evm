const crypto = require('crypto');
const bip39 = require('bip39');
const ethUtil = require('ethereumjs-util');
const hdkey = require('ethereumjs-wallet').hdkey;

class ETH {
  // Generate random entropy (bits should be 128, 160, 192, 224, or 256)
  static randEntropy(bits) {
    // Ensure bits is at least 128 (BIP39 requirement)
    const actualBits = Math.max(128, bits);
    const bytes = actualBits / 8;
    return crypto.randomBytes(bytes).toString('hex');
  }

  // Generate seed phrase from entropy
  static toSeedPhrase(entropy) {
    // Ensure entropy is valid for BIP39 (must be 128-256 bits and multiple of 32)
    const entropyBuffer = Buffer.from(entropy, 'hex');
    return bip39.entropyToMnemonic(entropyBuffer);
  }

  // Generate seed from seed phrase
  static toSeed(seedPhrase) {
    return bip39.mnemonicToSeedSync(seedPhrase).toString('hex');
  }

  // Generate private key from seed
  static toPrivateKey(seed) {
    const hdwallet = hdkey.fromMasterSeed(Buffer.from(seed, 'hex'));
    const wallet = hdwallet.derivePath("m/44'/60'/0'/0/0").getWallet();
    return wallet.getPrivateKey().toString('hex');
  }

  // Get public key from private key
  static getPublicKey(privateKey) {
    const privateKeyBuffer = Buffer.from(privateKey, 'hex');
    const publicKey = ethUtil.privateToPublic(privateKeyBuffer);
    return publicKey.toString('hex');
  }

  // Generate address from private key
  static toAddress(privateKey) {
    const privateKeyBuffer = Buffer.from(privateKey, 'hex');
    const addressBuffer = ethUtil.privateToAddress(privateKeyBuffer);
    return ethUtil.toChecksumAddress('0x' + addressBuffer.toString('hex'));
  }
}

// Example usage
async function generateWallet() {
  try {
    // Generate Random Entropy (128 bits minimum for BIP39)
    const entropy = ETH.randEntropy(128);
    console.log("Entropy:", entropy);

    // Generate Seed Phrase From Entropy
    const seedPhrase = ETH.toSeedPhrase(entropy);
    console.log("Seed Phrase:", seedPhrase);

    // Generate Seed From Seed Phrase
    const seed = ETH.toSeed(seedPhrase);
    console.log("Seed:", seed);

    // Generate Private Key From The Seed
    const privateKey = ETH.toPrivateKey(seed);
    console.log("Private Key:", privateKey);

    // Generate Public Key From Private Key
    const publicKey = ETH.getPublicKey(privateKey);
    console.log("Public Key:", publicKey);

    // Generate Address From Private Key
    const address = ETH.toAddress(privateKey);
    console.log("ETH Address:", address);
  } catch (error) {
    console.error("Error generating wallet:", error);
  }
}

generateWallet();

// To run this code, you need to install the following packages:
// npm install crypto bip39 ethereumjs-util ethereumjs-wallet