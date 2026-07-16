# spdocker
A) To run the docker container ..... run 


sudo su
docker compose --profile all -f compose-server.yml up -d --build


B) To see the stdout of the docker 

docker logs -f IMG_NAME

find out IMG_NAME by running 

docker ps -q

or if only one container is running 

docker logs -f $(docker ps -q)


C) To transfer file named FILENAME to IPADDR:PORT run

docker exec IMG_NAME ./client -s 1 -f FILENAME -i IPADDR:PORT 
