# How to find the commit that broke a test

<p class="page-intro">Use this when a test that used to pass is failing now, somewhere between two points in your history, and you do not want to read every commit in between by eye. It walks through turning "something broke" into one exact commit, and then into a small program short enough to read in one sitting.</p>

## Before you start

- The workbench from [0. The workbench](../guide/stage-0-workbench.md) has to exist: a build you can run from a clean checkout with one command, and a test runner that finds test cases on its own and gives each one a distinct pass or fail. This recipe drives that one command from a script; if building or testing still takes several manual steps, settle that first.
- You can reproduce the failure right now, on the commit you are standing on: build, run the one test, and watch it fail. Everything below only automates a check you have already done once by hand.
- History contains a point where the test passed. If the test is new and has never passed, there is no good commit to search from; write the fix directly instead of bisecting for it.
- [11. Release](../guide/stage-11-release.md) names the discipline this recipe protects: running every earlier test after every change, so that a regression is caught loudly, close to the change that caused it, instead of quietly, weeks later, by whoever bisects it by hand.

## Steps

1. **Reproduce the failure once, and write down exactly what "bad" looks like.** Run the failing test by hand and record the precise difference: the wrong printed bytes, the wrong exit status, or, if a diagnostic fired at all, its category and position instead of the ones the test expects ([Architecture: diagnostics](../architecture.md#diagnostics)). If the compiler crashes instead of producing output, capture one stack trace so you can recognize this crash later instead of a different one:

    ```sh
    lldb -b -o run -o bt -- ./vortex tests/invalid/open_string.vx
    ```

    Whatever you write down here is the one question every commit in history is about to be asked.

2. **Narrow the question to one test.** `git bisect` asks "is this commit good or bad?" at dozens of points in your history, and if the answer comes from running the whole suite, any unrelated failure at any of those points sends the search after the wrong change. Run only the test that regressed. If your test runner can select a single case by name, use that; if it cannot yet, run `vortex` on the failing test's input directly and compare its output with the recorded expected file yourself, the same comparison the runner does internally:

    ```sh
    diff tests/expected/open_string.out <(./vortex tests/invalid/open_string.vx 2>&1; echo "exit:$?")
    ```

3. **Find a commit where the test passed.** This is the search's other end.

    ```sh
    git log --oneline --diff-filter=A -- tests/invalid/open_string.vx tests/expected/open_string.out
    ```

    finds the commit that added the test's input and expected files; that commit, or any later one before the failure starts, is a candidate. If the test predates your memory of it working, pick any older commit you are confident had it passing, and let step 6's skip result handle anything older than the test file itself.

4. **Write the judge as a script that exits 0, 1 or 125, and nothing else.** `git bisect run` treats exactly these three exit statuses differently. Exit 0 means this commit is good. Any exit status from 1 to 127 except 125 means it is bad. Exit 125 means skip: this commit cannot be judged at all, so `git bisect` tries a different one instead of narrowing toward it. A commit that fails to build is not a commit where the test failed; it is a commit you have no answer for, so it exits 125, not 1, and so does a commit from before the test file existed. Any other exit status, including 126 for "found but not executable" and 127 for "not found", stops the whole run instead of continuing it, so a typo in the script looks like the search itself crashing, not a skip.

    ```sh
    #!/bin/sh
    # bisect-open-string.sh

    cd /Users/saumya/vortex || exit 125

    # Substitute your own stage 0 build command here.
    ./build.sh > /tmp/bisect-build.log 2>&1 || exit 125

    TEST=tests/invalid/open_string.vx
    [ -f "$TEST" ] || exit 125                # the test did not exist yet

    ACTUAL=$(./vortex "$TEST" 2>&1)
    case "$ACTUAL" in
      *lexical*2:17*) exit 0 ;;               # still rejected correctly
      *)              exit 1 ;;               # accepted, or wrong reason
    esac
    ```

5. **Keep the script outside the repository.** `git bisect` checks out a different commit before every run of your script, and a script tracked inside the repository is checked out, and can change shape or disappear, along with everything else. A script that changes partway through the search is no longer judging every commit by the same rule, which is the one thing a bisection depends on to mean anything. Save it somewhere the checkouts never reach, for example your home directory, and make it runnable once:

    ```sh
    chmod +x ~/bisect-open-string.sh
    ```

6. **Run the search.** `git bisect` needs one bad point, one good point, and the script:

    ```sh
    git bisect start
    git bisect bad HEAD
    git bisect good <commit-from-step-3>
    git bisect run ~/bisect-open-string.sh
    ```

    Git checks out the midpoint of the remaining range, runs your script, reads its exit status, and repeats, halving the range each time, so a thousand commits between good and bad take about ten runs. When the range narrows to one commit, it prints a line naming that commit as the first bad one and stops.

7. **Confirm the answer, then leave bisect mode.** Before resetting, `git show <hash>` reads the named commit's diff; check that it touches something plausible for this test. A commit that only edits documentation, named as the first bad one, means an earlier step's script had a mistake, not that documentation broke a compiler test. `git bisect reset` then returns your checkout to the branch and commit you started from; run it even if you plan to look further, because until you do you are sitting on a detached commit somewhere in the middle of the search.

8. **Reduce the failing input, now that you have one commit and one program.** Take the failing test's Vortex source and apply [O12's test-case reduction](../../optimize/o12-testing-optimizers.md#test-case-reduction-from-a-page-to-a-sentence) to it by hand: delete one statement or one declaration at a time, keep the deletion only when the program still compiles and still shows the exact difference you wrote down in step 1, and stop once no single remaining deletion survives that test. What is left is the smallest program that still reaches the named commit's change, and reading that commit's diff against a program this small is far faster than reading it against the whole original test.

9. **Turn the reduced program into a permanent regression test.** File it as a new test case, with its own recorded expected output, next to the test that first caught the problem, following the format [0. The workbench](../guide/stage-0-workbench.md#what-goes-in-a-test-case) already sets out. Once the underlying change is fixed, this becomes exactly the kind of golden test [O12](../../optimize/o12-testing-optimizers.md#pass-tests-and-golden-tests) describes: cheap to run forever, and proof that this exact bug does not come back unnoticed.

## Check that it worked

- `git bisect run` names exactly one commit, and rebuilding by hand at that commit and at its parent reproduces good on the parent and bad on the commit itself; the printed answer is only trustworthy once you have seen both sides yourself.
- After `git bisect reset`, `git status` shows the same branch and commit you were on before `git bisect start`, not a detached head left over from the search.
- The reduced program from step 8 still fails with the identical difference recorded in step 1, whether that is the same wrong bytes, the same wrong exit status, or the same diagnostic category and position, never a different failure that happened to also be wrong.
- The reduced program, filed as a new test case, fails before the named commit's fix and passes after it, and the rest of the suite is unaffected: the regression check [11. Release](../guide/stage-11-release.md) asks for on every change, now covering this bug specifically.

## Related

- [0. The workbench](../guide/stage-0-workbench.md): the one-command build and test runner this recipe's script drives, and the format a new test case follows.
- [11. Release](../guide/stage-11-release.md): running every earlier test after a change, the discipline a regression slipped past.
- [O12. Testing an optimizer](../../optimize/o12-testing-optimizers.md): test-case reduction and bisection in general, applied here to the whole compiler's history instead of one pass's numbered transformations.
- [Compiler architecture](../architecture.md#diagnostics): what a diagnostic carries, when the difference you are chasing is a wrong category or position rather than wrong output.
- [Add a diagnostic](add-a-diagnostic.md): the format a reduced case's expected output should follow once it becomes a permanent test.
