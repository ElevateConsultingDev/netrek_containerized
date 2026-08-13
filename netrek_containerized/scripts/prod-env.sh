# Shared config for prod (EC2 STURGEON server). Sourced by prod-*.sh.
export AWS_PROFILE_NETREK=elevate
export AWS_REGION_NETREK=us-west-2
export NETREK_IID=i-0003a695c0c664b68
export NETREK_EIP=18.236.22.45
export NETREK_ALLOC=eipalloc-0968aac3d7f5b47c1
export NETREK_SG=sg-09aeb2fd1dbe393a8
export NETREK_HOST=sturgeon.elevateconsulting.dev   # falls back to EIP if DNS not set
aws_() { aws --profile "$AWS_PROFILE_NETREK" --region "$AWS_REGION_NETREK" "$@"; }
