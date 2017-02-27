# game-gl-demo-server

This project contains source trees for iFun Engine demo on AWS GameLift.

To build the directory, you need to install all the dev tools required iFun Engine.
(This process may be tricky. Please consult funapi-support@ifunfactory.com if you want to do so.)

The project has two subdirectories:

* ``field_and_instance``: shared source tree for the field servers and instance world server. These servers will run on GameLift.
* ``login_and_chat``: shared source tree for the login server and the chat server. These servers will run on EC2.

Though this project doesn't build right away, you can see how much the iFun Engine can simplify GameLift integration work.
