<?php

$host = '192.168.90.123';
$user = 'user0';
$pass = '1234';
$db = 'fat';

$id_card = $_GET['id_card'];
$action = $_GET['action'];

$conn = mysqli_connect($host, $user, $pass, $db) or die("Unable to connect");

$sql = "INSERT INTO access (id_karty,akcja) VALUES ('$id_card','$action')";
$sendQuery = mysqli_query($conn, $sql);

mysqli_close($conn);

?>