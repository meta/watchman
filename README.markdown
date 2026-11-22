<div align="center">
 <img src="website/static/img/logo.png" width="20%" height="20%" alt="watchman-logo">
 <h1>Watchman</h1>
 <h3>A file watching service.</h3>
</div>

## Purpose

Watchman exists to watch files and record when they actually change. It can
also trigger actions (such as rebuilding assets) when matching files change.

## Installation Instructions

macOS (via Homebrew)
```bash
brew install watchman 
```

Linux (Ubuntu)
```bash
sudo apt update && sudo apt install watchman
```

Windows
Download the latest release from the https://github.com/facebook/watchman/releases and follow the instalation instructions. 

## Basic Usage Guide
Once installed, you can verify Watchman is running:
```bash
watchman version
```

To start watching a directory:
```bash
watchman watch /path/to/directory
```

## Common Use Cases

Automated Asset Compilation 
* Watchman can be used to automatically detect file changes and trigger actions like rebuilding assets (e.g., compiling JavaScript, CSS, or other project files)

Efficient Testing Workflows
* Many testing frameworks (such as Jest) integrate with Watchman to rerun tests whenever a file changes

* This speeds up the development process by allowing real-time feedback instead of manually restarting tests

Version Control Integration
* It can be used to track changes in Git repositories

* Watchman helps to identify modified files and can be configured to alert teams or run scripts when changes occur

File Synchornization and Backup
* Watchman can be used to monitor directories and sync changes across different environments

## Documentation

Head on over to https://meta.github.io/watchman/

## License

Watchman is made available under the terms of the MIT License. See the
LICENSE file that accompanies this distribution for the full text of the
license.

## Support

Watchman is primarily maintained by the source control team at Meta Platforms, Inc. We support:

* Windows and macOS builds
* Linux builds on recent Ubuntu and Fedora releases
* Watchman's [compatibility commitment](https://meta.github.io/watchman/docs/compatibility.html)
* Python, Rust, and JavaScript clients

Support for additional operating systems, release packaging, and language bindings is community-maintained:

* Homebrew
* FreeBSD
* Solaris

Please submit a [GitHub issue](https://github.com/meta/watchman/issues/) to report any troubles.

## Contributing

Please see the [contributing document](CONTRIBUTING.md).
