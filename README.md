# Discord Bot in C with Gemini AI 🤖✨

A high-performance Discord bot written in **Native C** for Windows.

## 🌟 Features

- **Context-Aware Chat**: Remembers recent conversation history per channel.
- **Native Performance**: Built with WinHTTP, no heavy runtimes like Node.js or Python required.
- **Gemini AI Integration**: Supports Gemini 1.5 Flash/Pro with custom system prompts (character personality).
- **Multi-threaded**: Handles AI requests in the background without blocking the bot's connection.
- **Automatic Setup**: Automatically fetches Application ID using only your Bot Token.

---

## 🚀 Getting Started

### 1. Prerequisites
- **Compiler**: [MinGW-w64](https://www.mingw-w64.org/) (GCC) or **Visual Studio** (MSVC).
- **Discord Bot Token**: Create one at the [Discord Developer Portal](https://discord.com/developers/applications).
- **Gemini API Key**: Get one from [Google AI Studio](https://aistudio.google.com/).

### 2. Configuration
Rename `config.example.json` to `config.json` and fill in your keys:

```json
{
    "token": "YOUR_DISCORD_TOKEN",
    "apis": {
        "gemini": {
            "api_key": "YOUR_GEMINI_KEY",
            "model": "gemini-1.5-flash"
        }
    },
    "chat": {
        "character_prompt": "You are Ryo, a sassy and funny AI assistant."
    }
}
```

### 3. Build & Run
Run the provided build script:
```cmd
build.bat
```
Then start the bot:
```cmd
bot.exe
```

---

## 🛠️ Commands
- `/chat <message>`: Talk to the AI.
- `/clear`: Clear the bot's memory for the current channel.
- `!ping`: A classic test command.



## 📄 License
This project is open-source and available under the [MIT License](LICENSE).
