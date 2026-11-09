- **Chat is a playground now.** A settings panel above the conversation
  (*Hide settings* / *Show settings*) chooses the model: the endpoint's
  default, a model its server lists, or any model id typed in. It also
  sets the system prompt, the temperature, top p and the most output
  tokens, and declares tools as JSON (`[{"name", "description",
  "parameters"}]`). An out-of-range number or broken tools JSON is
  reported before anything is sent. When the model calls a tool, the call
  and its arguments are shown and the next message goes back as the
  tool's result. The status line reads out the model and the tokens of
  the last reply and of the whole chat.
