let nextRequestId = 1;
const pendingCommands = new Map();
function runCommand(command, payload = "") {
  const requestId = nextRequestId++;

  return new Promise((resolve, reject) => {
    pendingCommands.set(requestId, { resolve, reject });

    if (socket.readyState !== WebSocket.OPEN) {
      pendingCommands.delete(requestId);
      reject(new Error("WebSocket is not connected"));
      return;
    }

    socket.send(JSON.stringify({
      command,
      payload,
      requestId
    }));

    setTimeout(() => {
      if (pendingCommands.has(requestId)) {
        pendingCommands.delete(requestId);
        reject(new Error("Command timed out"));
      }
    }, 15000);
  });
}