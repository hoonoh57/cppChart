# CI status expectations

A branch is ready for local synchronization only when the Windows CI workflow has completed successfully for the branch head.

The verified artifact is `shell.exe`, uploaded by the workflow for seven days.

A local pull is not requested for documentation-only or deterministic core changes unless an interactive acceptance checkpoint has been reached.
