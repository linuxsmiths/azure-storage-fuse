## blobfuse2 completion zsh

Generate the autocompletion script for zsh

### Synopsis

Generate the autocompletion script for the zsh shell.

If shell completion is not already enabled in your environment you will need
to enable it.  You can execute the following once:

	echo "autoload -U compinit; compinit" >> ~/.zshrc

To load completions in your current shell session:

	source <(blobfuse2 completion zsh); compdef _blobfuse2 blobfuse2

To load completions for every new session, execute once:

#### Linux:

	blobfuse2 completion zsh > "${fpath[1]}/_blobfuse2"

#### macOS:

	blobfuse2 completion zsh > /usr/local/share/zsh/site-functions/_blobfuse2

You will need to start a new shell for this setup to take effect.


```
blobfuse2 completion zsh [flags]
```

### Options

```
  -h, --help              help for zsh
      --no-descriptions   disable completion descriptions
```

### Options inherited from parent commands

```
      --disable-version-check   To disable version check that is performed automatically
```

### SEE ALSO

* [blobfuse2 completion](blobfuse2_completion.md)	 - Generate the autocompletion script for the specified shell

###### Auto generated by spf13/cobra on 15-Sep-2022