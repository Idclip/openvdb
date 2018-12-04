# JIRA Workflow

This document sets out the standardized JIRA workflow for use within the [ASWF OpenVDB JIRA Project.](https://jira.aswf.io/projects/OVDB)

## JIRA Issues

  * JIRA issues are to be granular and individual pieces of work designed for a single assignee.
  * JIRA issues should only be created by members of the TSC.
  * In general, all JIRA issues are expected to contain the following:
    * A concise summary title of the issue.
    * A best attempt at priority classification pending TSC discussion.
    * A detailed description of the issue along with any additional content relating to replicating or describing the work.
    * A best attempt at time estimation. This can be left blank in the case of Task issue types which are created for discussion.
    * Any linked issues which relate to the work.
    * A label describing their origin source and a link to external data if present (e.g. forums/github issues).
    * An active OVDB Epic link.
  * Depending on the nature of the task, different members of the TSC may be better suited to address the work. This should be made clear by effective descriptions, categorization and tagging of TSC members on the given issue.
  * The following should be kept in mind when writing issue descriptions:
    * In theory, any member of the TSC should be able to pick up and work on any open JIRA issue.
    * The issues are publicly visible and exist as a log for all OpenVDB users.

## Issue Type Categorization

### [![][JIRABugs] Bugs](https://jira.aswf.io/issues/?filter=10003)

  * A Bug is defined as an unexpected result of an OpenVDB operation.
  * Examples of Bugs are:
    * Non deterministic behavior
    * Incorrect results from operators
    * Unexpected assertions or segmentation faults
  * When creating a Bug Issue Type, additional information is required. At a minimum, the issue must contain the following relative to where the bug behavior was observed:
    * The Platform description. This can typically be provided on Linux systems with `uname -a`
    * The Operating System. This can typically be provided on Linux systems with `lsb_release -a`
    * The SHA1 ID of the OpenVDB Repository where the behavior was last observed if possible.
    * The VDB Version where the behavior was last observed.
    * Any other relevant application version information.

### [![][JIRAImprovements] Improvements](https://jira.aswf.io/issues/?filter=10004)

  * An Improvement is any change that pertains to existing implementation within the OpenVDB code-base.
  * Improvements are primarily designed at retaining algorithmic results whilst improving their underlying implementation.
  * Improvements are allowed to have minor behavior changing impacts.
  * Examples of Improvements are:
    * Code refactoring e.g. Changing API for better usage and clearer descriptions.
    * Code efficiency changes e.g. faster/more memory efficient implementations.
    * Improvements to result quality e.g. Increased accuracy of calculations.
    * API changes/updates to existing methods.

### [![][JIRANewFeatures] New Features](https://jira.aswf.io/issues/?filter=10005)

  * A New Feature is any change, extension or addition to the OpenVDB code-base which either adds new functionality or significantly alters the results of existing tools.
  * Examples of New Features are:
    * The addition of new independent functionality e.g. New unit tested translation units.
    * Changes or extensions to existing methods and/or architecture. e.g. Extending a free functions signature with a new argument for new behavior.

### [![][JIRATasks] Tasks](https://jira.aswf.io/issues/?filter=10006)

  * A Task is work that pertains to OpenVDB but may not necessarily require developer implementation.
  * Examples of Tasks are:
    * Performing a Release of OpenVDB.
    * Scheduling time for a code review.
    * Tasks to start discussion either within the TSC or with outside users relating to relevant topics.

## Epics

  * We use JIRA Epics as the main way to categorize JIRA issues. Whenever a new issue is created, it must be assigned to one of the existing Epics listed below:
    * [`OpenVDB Core`](https://jira.aswf.io/browse/OVDB-4) - Issues relating to the core OpenVDB repository, core OpenVDB data structure and core algorithms.
    * [`OpenVDB Points`](https://jira.aswf.io/browse/OVDB-3) - Issues relating to core data structure and algorithms for OpenVDB Points.
    * [`OpenVDB Houdini`](https://jira.aswf.io/browse/OVDB-2) - Issues relating to the OpenVDB Houdini plug-in.
    * `OpenVDB Maya` - Issues relating to the OpenVDB Maya plug-in.
    * `OpenVDB Infrastructure` - Issues relating to the repository and code layout, formatting, structure, testing and example material.
    * `OpenVDB Build Systems` - Issues relating to the CMake, Makefile and other build systems pertaining to the OpenVDB repository.

## Labels

  * Labels are used to track source tags for created JIRA issues.
  * Tickets can be assigned multiple labels if the issues pertains to multiple sources.

## Boards

  * @todo

[JIRABugs]:https://jira.aswf.io/secure/viewavatar?size=xsmall&avatarId=10303&avatarType=issuetype
[JIRAImprovements]:https://jira.aswf.io/secure/viewavatar?size=xsmall&avatarId=10310&avatarType=issuetype
[JIRANewFeatures]:https://jira.aswf.io/secure/viewavatar?size=xsmall&avatarId=10311&avatarType=issuetype
[JIRATasks]:https://jira.aswf.io/secure/viewavatar?size=xsmall&avatarId=10318&avatarType=issuetype
