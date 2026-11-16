#pragma once

// Thinking minds: a few people a year get a real mind, a language model running on the player's
// own machine (Ollama, free and offline). It sees only what that person could know, in plain
// words, and answers with a thought, something to try making and something to work toward. The
// world's own rules then decide whether any of it works (see People::Think).

#include <cstdio>
#include <functional>
#include <string>

namespace earth
{
// Asks a mind one question and returns its answer, or "" if no mind could be reached.
using Thinker = std::function<std::string(const std::string& Prompt)>;

namespace minds
{
inline std::string JsonEscape(const std::string& S)
{
	std::string O;
	for (const char C : S)
	{
		switch (C)
		{
		case '"': O += "\\\""; break;
		case '\\': O += "\\\\"; break;
		case '\n': O += "\\n"; break;
		case '\r': break;
		case '\t': O += "\\t"; break;
		default:
			if (static_cast<unsigned char>(C) >= 0x20) O += C;
		}
	}
	return O;
}

// The value of Key in a flat JSON object, as text (strings unescaped); "" if missing.
// shortcut: no nested objects, and \uXXXX reads as '?'; enough for the flat answers asked for.
inline std::string JsonString(const std::string& Json, const std::string& Key)
{
	const std::string Quoted = "\"" + Key + "\"";
	size_t At = Json.find(Quoted);
	if (At == std::string::npos) return "";
	At = Json.find(':', At + Quoted.size());
	if (At == std::string::npos) return "";
	At = Json.find_first_not_of(" \t\r\n", At + 1);
	if (At == std::string::npos) return "";
	if (Json[At] != '"') // A number or word: up to the next comma or brace.
	{
		const size_t End = Json.find_first_of(",}\n", At);
		std::string V = Json.substr(At, End == std::string::npos ? std::string::npos : End - At);
		while (!V.empty() && (V.back() == ' ' || V.back() == '\r')) V.pop_back();
		return V == "null" ? "" : V;
	}
	std::string O;
	for (size_t i = At + 1; i < Json.size(); ++i)
	{
		const char C = Json[i];
		if (C == '"') return O;
		if (C != '\\' || i + 1 >= Json.size())
		{
			O += C;
			continue;
		}
		const char E = Json[++i];
		if (E == 'n') O += '\n';
		else if (E == 't') O += '\t';
		else if (E == 'u') O += '?', i += 4;
		else O += E;
	}
	return O;
}

#ifdef _WIN32
#define EARTH_POPEN _popen
#define EARTH_PCLOSE _pclose
#else
#define EARTH_POPEN popen
#define EARTH_PCLOSE pclose
#endif

// A mind served by Ollama on this machine (https://ollama.com), reached with curl, which Windows
// 10 and later include. BodyPath is a scratch file for the request.
inline Thinker OllamaThinker(const std::string& Model, const std::string& BodyPath, const std::string& Url = "http://localhost:11434/api/generate")
{
	return [=](const std::string& Prompt) -> std::string
	{
		if (std::FILE* F = std::fopen(BodyPath.c_str(), "wb"))
		{
			const std::string Body = "{\"model\":\"" + JsonEscape(Model) + "\",\"prompt\":\"" + JsonEscape(Prompt) +
				"\",\"format\":\"json\",\"stream\":false,\"options\":{\"temperature\":0.8}}";
			std::fwrite(Body.data(), 1, Body.size(), F);
			std::fclose(F);
		}
		else
		{
			return "";
		}
		const std::string Command = "curl -s --noproxy localhost,127.0.0.1 --max-time 180 -H \"Content-Type: application/json\" --data-binary @\"" + BodyPath + "\" " + Url;
		std::string Out;
		if (std::FILE* P = EARTH_POPEN(Command.c_str(), "r"))
		{
			char Buf[4096];
			size_t N = 0;
			while ((N = std::fread(Buf, 1, sizeof(Buf), P)) > 0) Out.append(Buf, N);
			EARTH_PCLOSE(P);
		}
		std::remove(BodyPath.c_str());
		return JsonString(Out, "response");
	};
}
} // namespace minds
} // namespace earth
