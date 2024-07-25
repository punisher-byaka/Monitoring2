const express = require('express');
const bodyParser = require('body-parser');
const fs = require('fs');
const path = require('path');

const app = express();
const port = 3000;

let clients = [];

app.use(bodyParser.json());
app.use(express.static('public'));

// Route for receiving monitoring data
app.post('/api/monitor', (req, res) => {
    const { user, ip, lastActive } = req.body;
    console.log(`Monitor data received: user=${user}, ip=${ip}, lastActive=${lastActive}`);

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
    console.log(`Receiving screenshot for user: ${user}`);

    // Replace colons in the timestamp with dashes
    timestamp = timestamp.replace(/:/g, '-');

    req.on('data', chunk => {
        chunks.push(chunk);
    });

    req.on('end', () => {
        const screenshotBuffer = Buffer.concat(chunks);
        const screenshotPath = path.join(__dirname, 'public', 'screenshots', `${user}_${timestamp}.jpeg`);
        console.log(`Saving screenshot to: ${screenshotPath}`);

        // Ensure the directory exists
        const screenshotDir = path.dirname(screenshotPath);
        console.log(`Checking if directory exists: ${screenshotDir}`);
        if (!fs.existsSync(screenshotDir)) {
            console.log(`Directory does not exist. Creating: ${screenshotDir}`);
            fs.mkdirSync(screenshotDir, { recursive: true });
        } else {
            console.log(`Directory already exists: ${screenshotDir}`);
        }

        try {
            console.log(`Writing file: ${screenshotPath}`);
            fs.writeFileSync(screenshotPath, screenshotBuffer);
            console.log(`Screenshot saved successfully.`);

            // Add screenshot to client data
            const clientIndex = clients.findIndex(client => client.user === user);
            if (clientIndex !== -1) {
                clients[clientIndex].screenshots.push(`${user}_${timestamp}.jpeg`);
            }

            res.sendStatus(200);
        } catch (error) {
            console.error(`Error saving screenshot for user ${user}:`, error);
            res.sendStatus(500);
        }
    });
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
    console.log(`Server listening at http://localhost:${port}`);
});
