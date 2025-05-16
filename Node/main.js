const FS = require("fs");
const ReadLine = require("readline");
const WebSocket = require('ws');
const Web3 = require('web3'); 

async function getMultipleBalancesWSS(addresses) {
    return new Promise((resolve, reject) => {
        const pendingRequests = new Map();
        const results = {};
        let nextId = 1;
        
        const ws = new WebSocket('wss://bsc.drpc.org');
            ws.on('open', () => {
                console.log('WebSocket connection established');
                console.time('Balance fetch operation');
                
                addresses.forEach(address => {
                    const id = nextId++;
                    const request = {
                    jsonrpc: '2.0',
                    method: 'eth_getBalance',
                    params: [address, 'latest'],
                    id
                };
                
                pendingRequests.set(id, address);
                ws.send(JSON.stringify(request));
            });
        });
    
        ws.on('message', (data) => {
            const response = JSON.parse(data.toString());
            const { id, result } = response;
            
            if (pendingRequests.has(id)) {                const address = pendingRequests.get(id);
                pendingRequests.delete(id);
                
                const balanceInWei = result;
                let balanceInEth;
                try {
                    balanceInEth = Web3.utils.fromWei(result, 'ether');
                } catch (error) {
                    const wei = parseInt(result, 16);
                    balanceInEth = wei / 1e18;
                }
            
                results[address] = {
                    hex: result,
                    wei: balanceInWei,
                    eth: balanceInEth
                };
            
                if (pendingRequests.size === 0) {
                    console.timeEnd('Balance fetch operation');
                    ws.close();
                    resolve(results);
                }
            }
        });
    
        ws.on('error', (error) => {
            console.error('WebSocket error:', error);
            reject(error);
        });
    

        setTimeout(() => {
            if (pendingRequests.size > 0) {
                const pending = Array.from(pendingRequests.values());
                console.warn(`Timeout: ${pending.length} addresses did not receive responses`);
                ws.close();
                resolve(results); 
            }
        }, 15000); 
    });
}

const collectLines = async function() {
    let lines = [];
    let readStream = FS.createReadStream("addresses");

    const nl = ReadLine.createInterface({
        input: readStream,
        crlfDelay: Infinity
    });

    for await (let line of nl) {
        lines.push(line)
    }

    return lines;
}

async function fetchMultipleBalances() {
    let array = await collectLines();

    try {
        console.log("\n=== NATIVE WEBSOCKET METHOD ===");
        const results = await getMultipleBalancesWSS(array);
        console.log("WebSocket Results:");
        
        Object.entries(results).map(([address, balance], index) => {
            if(balance.eth > 0.0001) {
                FS.appendFileSync("found.txt", `-->>> ${address} :: ${index} :: ${balance.eth} ->> \n`);
            }

            console.log(`${index}::${address}: ${balance.eth} ETH`);
        });
        
        return results;
    } catch (error) {
        console.error('Error fetching balances:', error);
    }
}

// Run the function
fetchMultipleBalances();