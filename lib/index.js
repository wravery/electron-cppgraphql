// Modules to control application life and handle IPC
const { app, ipcMain } = require("electron");

const graphql = require("bindings")("electron-cppgraphql.node");
let serviceStarted = false;

function startService() {
  graphql.startService();
  serviceStarted = true;
}

function stopService() {
  serviceStarted = false;
  graphql.stopService();
}

exports.startGraphQL = function() {
  // Register the IPC callbacks
  ipcMain.handle("startService", startService);
  ipcMain.handle("stopService", stopService);
  ipcMain.handle("parseQuery", (_event, query) => graphql.parseQuery(query));
  ipcMain.handle("discardQuery", (_event, queryId) =>
    graphql.discardQuery(queryId)
  );
  ipcMain.on("fetchQuery", (event, queryId, operationName, variables) =>
    graphql.fetchQuery(
      queryId,
      operationName,
      variables,
      (payload) => {
        if (serviceStarted) {
          event.reply("fetched", queryId, payload);
        }
      },
      () => {
        if (serviceStarted) {
          event.reply("completed", queryId);
        }
      }
    )
  );
  ipcMain.handle("unsubscribe", (_event, queryId) =>
    graphql.unsubscribe(queryId)
  );

  // Quit when all windows are closed.
  app.on("window-all-closed", stopService);
};

exports.preloadPath = require.resolve("./preload");
