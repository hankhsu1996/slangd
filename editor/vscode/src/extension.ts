import * as path from "path";
import * as vscode from "vscode";
import * as fs from "fs";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient;
let outputChannel: vscode.OutputChannel;
let statusBarItem: vscode.StatusBarItem;

export function activate(context: vscode.ExtensionContext) {
  outputChannel = vscode.window.createOutputChannel(
    "SystemVerilog Language Server"
  );

  // Create status bar item (left side, low priority)
  statusBarItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    0
  );
  statusBarItem.text = "slangd: idle";
  statusBarItem.show();
  context.subscriptions.push(statusBarItem);

  // Get the path to the server executable from the settings
  const config = vscode.workspace.getConfiguration("systemverilog");
  let serverPath = config.get<string>("server.path");

  // If no path is provided, use the default bundled path
  if (!serverPath) {
    const extensionPath = context.extensionPath;

    // Check if binary is available in the extension's bin directory first
    const bundledPath = path.join(extensionPath, "bin", "slangd");
    const projectPath = path.resolve(extensionPath, "..", "..");
    const bazelBuildPath = path.join(projectPath, "bazel-bin", "slangd");

    // Try the bundled path first, then fallback to the bazel build path
    if (fs.existsSync(bundledPath)) {
      serverPath = bundledPath;
    } else if (fs.existsSync(bazelBuildPath)) {
      serverPath = bazelBuildPath;
    } else {
      // Try platform-specific executable names (for Windows)
      const winBundledPath = bundledPath + ".exe";
      const winBazelPath = bazelBuildPath + ".exe";

      if (fs.existsSync(winBundledPath)) {
        serverPath = winBundledPath;
      } else if (fs.existsSync(winBazelPath)) {
        serverPath = winBazelPath;
      } else {
        const errorMsg =
          "Could not find slangd executable. Please set the path in settings.";
        outputChannel.appendLine(`ERROR: ${errorMsg}`);
        vscode.window.showErrorMessage(errorMsg);
        return;
      }
    }
  }

  outputChannel.appendLine(`Using slangd server at: ${serverPath}`);

  // Get log level from settings
  const logLevel = config.get<string>("server.logLevel", "debug");
  outputChannel.appendLine(`Log level: ${logLevel}`);

  // Server options - using pipe transport for VSCode
  const serverOptions: ServerOptions = {
    run: {
      command: serverPath,
      transport: TransportKind.pipe,
      options: {
        env: { ...process.env, SPDLOG_LEVEL: logLevel },
      },
    },
    debug: {
      command: serverPath,
      transport: TransportKind.pipe,
      options: {
        env: { ...process.env, SPDLOG_LEVEL: logLevel },
      },
    },
  };

  // Options to control the language client
  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "systemverilog" }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/*.{sv,svh,v}"),
    },
    outputChannel,
    traceOutputChannel: outputChannel,
    initializationOptions: {
      trace: { server: "verbose" },
    },
  };

  client = new LanguageClient(
    "systemverilog",
    "SystemVerilog Language Server",
    serverOptions,
    clientOptions
  );

  // Listen for status notifications from server (before starting client)
  client.onNotification("$/slangd/status", (params: { status: string }) => {
    statusBarItem.text = `slangd: ${params.status}`;
  });

  // Handle client errors
  client.onDidChangeState((event) => {
    if (event.newState === 1) {
      // State.Running
      outputChannel.appendLine("Language server is running");
    }
  });

  try {
    client.start();

    context.subscriptions.push({
      dispose: () => {
        if (client) {
          return client.stop();
        }
      },
    });
  } catch (error) {
    outputChannel.appendLine(
      `ERROR: Failed to start language server: ${error}`
    );
    vscode.window.showErrorMessage(
      `Failed to start SystemVerilog Language Server: ${error}`
    );
  }

  // Register restart command
  const restartCommand = vscode.commands.registerCommand(
    "systemverilog.restartServer",
    async () => {
      if (!client) {
        vscode.window.showWarningMessage(
          "SystemVerilog language server is not running"
        );
        return;
      }

      try {
        outputChannel.appendLine("Restarting language server...");
        await client.stop();
        await client.start();
        outputChannel.appendLine("Language server restarted successfully");
        vscode.window.showInformationMessage(
          "SystemVerilog language server restarted"
        );
      } catch (error) {
        outputChannel.appendLine(
          `ERROR: Failed to restart language server: ${error}`
        );
        vscode.window.showErrorMessage(
          `Failed to restart SystemVerilog language server: ${error}`
        );
      }
    }
  );
  context.subscriptions.push(restartCommand);
}

export function deactivate(): Thenable<void> | undefined {
  if (!client) {
    return undefined;
  }
  return client.stop();
}
