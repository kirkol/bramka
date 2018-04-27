<?php

$host = '192.168.90.123';
$user = 'user0';
$pass = '1234';
$db = 'fat';

$id_card = $_GET['id_card'];
$action = $_GET['action'];
$data = $_GET['data'];

$data = str_replace("H", " ", $data);

$conn = mysqli_connect($host, $user, $pass, $db) or die("Unable to connect");

$sql = "INSERT INTO access (id_karty,akcja,data) VALUES ('$id_card','$action','$data')";
$sendQuery = mysqli_query($conn, $sql);

mysqli_close($conn);

?>