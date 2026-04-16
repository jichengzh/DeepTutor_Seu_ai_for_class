"use client";

import { useState, useRef, useEffect, useCallback } from "react";
import { Send, Loader2, CheckCircle, X } from "lucide-react";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { useTranslation } from "react-i18next";
import { wsUrl } from "@/lib/api";

interface ChatMessage {
  role: "user" | "assistant";
  content: string;
  revisedSection?: string | null; // parsed from <revised_section>
  accepted?: boolean;             // whether revision was accepted
}

interface Props {
  sectionKey: string;
  sectionTitle: string;
  sectionContent: string;
  chatHistory: ChatMessage[];
  onChatUpdate: (sectionKey: string, messages: ChatMessage[]) => void;
  onAcceptRevision: (sectionKey: string, newContent: string) => void;
}

export default function ReviewPanel({
  sectionKey,
  sectionTitle,
  sectionContent,
  chatHistory,
  onChatUpdate,
  onAcceptRevision,
}: Props) {
  const { t } = useTranslation();
  const [input, setInput] = useState("");
  const [streaming, setStreaming] = useState(false);
  const [streamBuffer, setStreamBuffer] = useState("");
  const messagesEndRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLTextAreaElement>(null);

  // Auto-scroll to bottom
  useEffect(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [chatHistory, streamBuffer]);

  // Focus input on section change
  useEffect(() => {
    inputRef.current?.focus();
  }, [sectionKey]);

  const handleSend = useCallback(async () => {
    const message = input.trim();
    if (!message || streaming) return;

    setInput("");
    setStreaming(true);
    setStreamBuffer("");

    // Add user message
    const updatedHistory: ChatMessage[] = [
      ...chatHistory,
      { role: "user", content: message },
    ];
    onChatUpdate(sectionKey, updatedHistory);

    // Build chat history for API (exclude revisedSection metadata)
    const apiHistory = chatHistory.map((m) => ({
      role: m.role,
      content: m.content,
    }));

    try {
      const ws = new WebSocket(
        (process.env.NEXT_PUBLIC_API_BASE || "http://localhost:8001")
          .replace(/^http:/, "ws:")
          .replace(/^https:/, "wss:") + "/api/v1/project/review-chat"
      );

      let fullResponse = "";
      let revisedSection: string | null = null;

      ws.onopen = () => {
        ws.send(
          JSON.stringify({
            section_key: sectionKey,
            section_title: sectionTitle,
            section_content: sectionContent,
            message,
            chat_history: apiHistory,
          })
        );
      };

      ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        if (data.type === "chunk") {
          fullResponse += data.content;
          setStreamBuffer(fullResponse);
        } else if (data.type === "complete") {
          fullResponse = data.content;
          revisedSection = data.revised_section || null;
          setStreamBuffer("");
          setStreaming(false);

          // Strip <revised_section> tags from display text
          let displayContent = fullResponse;
          const startTag = "<revised_section>";
          const endTag = "</revised_section>";
          const startIdx = displayContent.indexOf(startTag);
          const endIdx = displayContent.indexOf(endTag);
          if (startIdx !== -1 && endIdx !== -1) {
            displayContent =
              displayContent.substring(0, startIdx).trim() +
              `\n\n*${t("[Modification suggestion generated, click button below to review and accept]")}*`;
          }

          const finalHistory: ChatMessage[] = [
            ...updatedHistory,
            {
              role: "assistant",
              content: displayContent,
              revisedSection,
            },
          ];
          onChatUpdate(sectionKey, finalHistory);
          ws.close();
        } else if (data.type === "error") {
          setStreaming(false);
          setStreamBuffer("");
          const errorHistory: ChatMessage[] = [
            ...updatedHistory,
            {
              role: "assistant",
              content: t("[Error] {msg}").replace("{msg}", String(data.content)),
            },
          ];
          onChatUpdate(sectionKey, errorHistory);
          ws.close();
        }
      };

      ws.onerror = () => {
        setStreaming(false);
        setStreamBuffer("");
      };

      ws.onclose = () => {
        setStreaming(false);
      };
    } catch {
      setStreaming(false);
      setStreamBuffer("");
    }
  }, [input, streaming, sectionKey, sectionTitle, sectionContent, chatHistory, onChatUpdate, t]);

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === "Enter" && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  };

  const handleAccept = (msgIndex: number) => {
    const msg = chatHistory[msgIndex];
    if (!msg?.revisedSection) return;

    onAcceptRevision(sectionKey, msg.revisedSection);

    // Mark as accepted in chat history
    const updated = chatHistory.map((m, i) =>
      i === msgIndex ? { ...m, accepted: true } : m
    );
    onChatUpdate(sectionKey, updated);
  };

  return (
    <div className="flex flex-col h-full">
      {/* Header */}
      <div className="px-4 py-3 border-b border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-800/50">
        <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-300 truncate">
          {t("Review: {section}").replace("{section}", sectionTitle)}
        </h3>
        <p className="text-xs text-gray-400 mt-0.5">
          {t("Enter modification suggestions for this section")}
        </p>
      </div>

      {/* Chat messages */}
      <div className="flex-1 overflow-y-auto px-4 py-3 space-y-4">
        {chatHistory.length === 0 && !streaming && (
          <div className="text-center text-gray-400 text-xs py-8">
            {t("Enter modification suggestions for 「{section}」 to start dialogue").replace(
              "{section}",
              sectionTitle,
            )}
          </div>
        )}

        {chatHistory.map((msg, i) => (
          <div key={i} className={`flex ${msg.role === "user" ? "justify-end" : "justify-start"}`}>
            <div
              className={`max-w-[85%] rounded-xl px-3.5 py-2.5 text-sm ${
                msg.role === "user"
                  ? "bg-blue-500 text-white rounded-br-sm"
                  : "bg-gray-100 dark:bg-gray-800 text-gray-800 dark:text-gray-200 rounded-bl-sm"
              }`}
            >
              {msg.role === "assistant" ? (
                <div className="prose prose-sm dark:prose-invert max-w-none [&>p]:my-1 [&>ul]:my-1">
                  <ReactMarkdown remarkPlugins={[remarkGfm]}>
                    {msg.content}
                  </ReactMarkdown>
                </div>
              ) : (
                <p className="whitespace-pre-wrap">{msg.content}</p>
              )}

              {/* Accept revision button */}
              {msg.role === "assistant" && msg.revisedSection && !msg.accepted && (
                <button
                  onClick={() => handleAccept(i)}
                  className="mt-2 flex items-center gap-1.5 px-3 py-1.5 bg-green-500 text-white rounded-lg text-xs font-medium hover:bg-green-600 transition-colors"
                >
                  <CheckCircle className="w-3.5 h-3.5" />
                  {t("Accept Changes")}
                </button>
              )}
              {msg.role === "assistant" && msg.accepted && (
                <div className="mt-2 flex items-center gap-1.5 text-green-600 dark:text-green-400 text-xs">
                  <CheckCircle className="w-3.5 h-3.5" />
                  {t("Changes Applied")}
                </div>
              )}
            </div>
          </div>
        ))}

        {/* Streaming indicator */}
        {streaming && (
          <div className="flex justify-start">
            <div className="max-w-[85%] rounded-xl rounded-bl-sm px-3.5 py-2.5 bg-gray-100 dark:bg-gray-800 text-sm">
              {streamBuffer ? (
                <div className="prose prose-sm dark:prose-invert max-w-none [&>p]:my-1">
                  <ReactMarkdown remarkPlugins={[remarkGfm]}>
                    {streamBuffer}
                  </ReactMarkdown>
                </div>
              ) : (
                <div className="flex items-center gap-2 text-gray-400">
                  <Loader2 className="w-3.5 h-3.5 animate-spin" />
                  {t("Thinking...")}
                </div>
              )}
            </div>
          </div>
        )}

        <div ref={messagesEndRef} />
      </div>

      {/* Input area */}
      <div className="px-4 py-3 border-t border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-900">
        <div className="flex gap-2">
          <textarea
            ref={inputRef}
            value={input}
            onChange={(e) => setInput(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder={t("Enter modification suggestion... (Enter to send, Shift+Enter for newline)")}
            rows={2}
            disabled={streaming}
            className="flex-1 resize-none rounded-lg border border-gray-200 dark:border-gray-700 px-3 py-2 text-sm bg-gray-50 dark:bg-gray-800 text-gray-800 dark:text-gray-200 focus:ring-2 focus:ring-blue-300 outline-none disabled:opacity-50"
          />
          <button
            onClick={handleSend}
            disabled={!input.trim() || streaming}
            className="self-end px-3 py-2 bg-blue-500 text-white rounded-lg hover:bg-blue-600 disabled:opacity-40 disabled:cursor-not-allowed transition-colors"
          >
            {streaming ? (
              <Loader2 className="w-4 h-4 animate-spin" />
            ) : (
              <Send className="w-4 h-4" />
            )}
          </button>
        </div>
      </div>
    </div>
  );
}
