# Custom aliases
alias ll='ls -latr'

# Environment variables
export EDITOR=vim
export PATH=$PATH:/usr/local/bin

# Prompt customization
PS1='\u@\h:\w\$ '

# Source vim config for convenience
alias vrc='vim ~/.vimrc'

set -o vi

export CFLAGS="-fcommon -Wno-error=format-overflow -U_FORTIFY_SOURCE -include stddef.h"

