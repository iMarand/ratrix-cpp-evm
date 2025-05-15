const crypto = require('crypto');
const bip39 = require('bip39');
const ethUtil = require('ethereumjs-util');
const hdkey = require('ethereumjs-wallet').hdkey;
const fs = require('fs');
const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');
const os = require('os');

class ETH {
  // Generate random entropy (bits should be 128, 160, 192, 224, or 256)
  static randEntropy(bits) {
    const actualBits = Math.max(128, bits);
    const bytes = actualBits / 8;
    return crypto.randomBytes(bytes).toString('hex');
  }

  // Generate all wallet data in one go to reduce function calls
  static generateWalletData(entropyBits = 128) {
    const entropy = this.randEntropy(entropyBits);
    const entropyBuffer = Buffer.from(entropy, 'hex');
    const seedPhrase = bip39.entropyToMnemonic(entropyBuffer);
    const seed = bip39.mnemonicToSeedSync(seedPhrase);
    
    const hdwallet = hdkey.fromMasterSeed(seed);
    const wallet = hdwallet.derivePath("m/44'/60'/0'/0/0").getWallet();
    const privateKey = wallet.getPrivateKey();
    const addressBuffer = ethUtil.privateToAddress(privateKey);
    const address = ethUtil.toChecksumAddress('0x' + addressBuffer.toString('hex'));
    
    return { entropy, address };
  }

  // Generate multiple wallets using worker threads
  static async generateMultipleWallets(count) {
    if (!isMainThread) {
      // Worker thread code
      const { start, end } = workerData;
      const results = [];
      
      for (let i = start; i < end; i++) {
        const walletData = ETH.generateWalletData(128);
        results.push(walletData);
      }
      
      parentPort.postMessage(results);
      return;
    }
    
    // Main thread code
    console.time('Generate wallets');
    
    const cpuCount = os.cpus().length;
    const batchSize = Math.ceil(count / cpuCount);
    const workers = [];
    const results = [];
    
    // Create worker promises
    const workerPromises = [];
    
    for (let i = 0; i < cpuCount; i++) {
      const start = i * batchSize;
      const end = Math.min(start + batchSize, count);
      
      if (start >= count) break;
      
      const promise = new Promise((resolve) => {
        const worker = new Worker(__filename, {
          workerData: { start, end }
        });
        
        worker.on('message', (data) => {
          results.push(...data);
          resolve();
        });
        
        workers.push(worker);
      });
      
      workerPromises.push(promise);
    }
    
    // Wait for all workers to complete
    await Promise.all(workerPromises);
    
    console.timeEnd('Generate wallets');
    
    // Extract addresses and entropy lists
    const addresses = results.map(item => item.address);
    const entropyList = results;
    
    // Use streams for file writing to improve memory efficiency
    const addressStream = fs.createWriteStream('addresses');
    const entropyStream = fs.createWriteStream('entropy');
    
    for (const address of addresses) {
      addressStream.write(address + '\n');
    }
    
    for (const item of entropyList) {
      entropyStream.write(`${item.entropy}:${item.address}\n`);
    }
    
    // Close streams
    await new Promise(resolve => {
      addressStream.end();
      addressStream.on('finish', resolve);
    });
    
    await new Promise(resolve => {
      entropyStream.end();
      entropyStream.on('finish', resolve);
    });
    
    console.log(`Saved ${addresses.length} addresses to 'addresses' file`);
    console.log(`Saved entropy data to 'entropy' file`);
    
    return { addresses, entropyList };
  }
}

// Example usage
async function generateWallets() {
  try {
    // Generate 1000 wallets and save to files
    const result = await ETH.generateMultipleWallets(1000);
    console.log(`Generated ${result.addresses.length} ETH addresses`);
  } catch (error) {
    console.error("Error generating wallets:", error);
  }
}

// Only run the example in the main thread
if (isMainThread) {
}
generateWallets();

// To run this code, you need to install the following packages:
// npm install crypto bip39 ethereumjs-util ethereumjs-wallet